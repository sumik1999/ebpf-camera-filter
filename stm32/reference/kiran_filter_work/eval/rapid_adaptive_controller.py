#!/usr/bin/env python3
"""RAPID network-segmentation controller for the airport test topology.

This implements Algorithm 1 from Himanshu's thesis using Linux switch-port
counters.  Each Linux bridge is an SDN switch, its veth devices are ports,
and the topology file assigns a sub-subnet (SSN) to every switch.  Counters
are sampled in windows, converted to the thesis' signed Delta1/Delta2
metrics, then used to block edge ports or meter/trace transit ports.

The controller intentionally does not call the CMS/ElasticSketch threshold
an "RAPID" metric: RAPID mitigation is port block, meter, and traceback.
"""

from __future__ import annotations

import argparse
import csv
import json
import signal
import subprocess
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable


@dataclass(frozen=True)
class Counter:
    rx: int
    tx: int

    def delta(self, old: "Counter") -> "Counter":
        # Interfaces can be recreated while the controller is starting.
        return Counter(
            self.rx - old.rx if self.rx >= old.rx else self.rx,
            self.tx - old.tx if self.tx >= old.tx else self.tx,
        )


@dataclass
class Port:
    name: str
    interface: str
    kind: str
    namespace: str | None = None
    trusted: bool = False
    peer_switch: str | None = None
    peer_port: str | None = None
    meter_rate: str = "10mbit"

    @property
    def key(self) -> str:
        return f"{self.namespace or 'root'}:{self.interface}"


@dataclass
class Switch:
    name: str
    subnet: str
    ports: list[Port] = field(default_factory=list)


class SysfsCounterBackend:
    """Read interface counters in the root namespace or a network namespace."""

    def read(self, port: Port) -> Counter:
        base = f"/sys/class/net/{port.interface}/statistics"
        if port.namespace:
            rx = self._run(["ip", "netns", "exec", port.namespace, "cat", f"{base}/rx_packets"])
            tx = self._run(["ip", "netns", "exec", port.namespace, "cat", f"{base}/tx_packets"])
        else:
            rx = Path(f"{base}/rx_packets").read_text(encoding="ascii")
            tx = Path(f"{base}/tx_packets").read_text(encoding="ascii")
        return Counter(int(rx.strip()), int(tx.strip()))

    @staticmethod
    def _run(command: list[str]) -> str:
        return subprocess.run(command, check=True, text=True, capture_output=True).stdout


class Actuator:
    """Apply RAPID block and meter decisions to Linux switch ports."""

    def __init__(self, dry_run: bool = False):
        self.dry_run = dry_run
        self.blocked: set[str] = set()
        self.metered: dict[str, Port] = {}
        self._nft_ready = False

    def _run(self, command: list[str], namespace: str | None = None) -> None:
        full = (["ip", "netns", "exec", namespace] if namespace else []) + command
        if self.dry_run:
            return
        subprocess.run(full, check=True, text=True, capture_output=True)

    def _ensure_nft(self) -> None:
        if self._nft_ready or self.dry_run:
            self._nft_ready = True
            return
        subprocess.run(["nft", "delete", "table", "bridge", "rapid_guard"],
                       check=False, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        self._run(["nft", "add", "table", "bridge", "rapid_guard"])
        self._run(["nft", "add", "chain", "bridge", "rapid_guard", "forward",
                   "{ type filter hook forward priority -200; policy accept; }"])
        self._nft_ready = True

    def block(self, port: Port) -> bool:
        if port.key in self.blocked:
            return False
        if port.namespace:
            raise RuntimeError("edge blocking currently requires a root bridge port")
        self._ensure_nft()
        self._run(["nft", "add", "rule", "bridge", "rapid_guard", "forward",
                   "iifname", port.interface, "counter", "drop",
                   "comment", "rapid-edge-block"])
        self.blocked.add(port.key)
        return True

    def meter(self, port: Port) -> bool:
        if port.key in self.metered:
            return False
        self._run(["tc", "qdisc", "replace", "dev", port.interface, "clsact"], port.namespace)
        self._run(["tc", "filter", "replace", "dev", port.interface, "ingress",
                   "protocol", "all", "pref", "10", "matchall", "action", "police",
                   "rate", port.meter_rate, "burst", "64k", "conform-exceed", "drop"],
                  port.namespace)
        self.metered[port.key] = port
        return True

    def cleanup(self) -> None:
        if self.dry_run:
            return
        subprocess.run(["nft", "delete", "table", "bridge", "rapid_guard"],
                       check=False, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        for port in self.metered.values():
            command = ["tc", "qdisc", "del", "dev", port.interface, "clsact"]
            full = (["ip", "netns", "exec", port.namespace] if port.namespace else []) + command
            subprocess.run(full, check=False, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


class RapidController:
    DELTA1_STANDARD = 0.15
    DELTA1_STRICT = 0.33
    DELTA2_PORT = 0.50

    def __init__(self, switches: list[Switch], backend=None, actuator=None,
                 minimum_packets: int = 50, confirmation_windows: int = 2,
                 event_sink: Callable[[dict], None] | None = None):
        self.switches = {switch.name: switch for switch in switches}
        self.backend = backend or SysfsCounterBackend()
        self.actuator = actuator or Actuator()
        self.minimum_packets = minimum_packets
        self.confirmation_windows = confirmation_windows
        self.event_sink = event_sink or (lambda event: None)
        self.previous: dict[str, Counter] = {}
        self.streaks: dict[str, int] = {}
        self._validate()

    def _validate(self) -> None:
        if not self.switches:
            raise ValueError("topology contains no switches")
        keys: set[str] = set()
        for switch in self.switches.values():
            if not switch.subnet:
                raise ValueError(f"switch {switch.name} has no assigned sub-subnet")
            for port in switch.ports:
                if port.kind not in {"edge", "transit"}:
                    raise ValueError(f"{switch.name}/{port.name}: kind must be edge or transit")
                if port.key in keys:
                    raise ValueError(f"interface appears more than once: {port.key}")
                keys.add(port.key)
                if port.kind == "transit" and port.peer_switch not in self.switches:
                    raise ValueError(f"{switch.name}/{port.name}: unknown peer switch")

    @staticmethod
    def balance(rx: int, tx: int) -> float:
        total = rx + tx
        return (rx - tx) / total if total else 0.0

    def prime(self) -> None:
        self.previous = {
            port.key: self.backend.read(port)
            for switch in self.switches.values() for port in switch.ports
        }

    def sample(self) -> tuple[dict[str, Counter], dict[str, float], dict[str, float]]:
        deltas: dict[str, Counter] = {}
        delta2: dict[str, float] = {}
        for switch in self.switches.values():
            for port in switch.ports:
                now = self.backend.read(port)
                old = self.previous.get(port.key, now)
                change = now.delta(old)
                self.previous[port.key] = now
                deltas[port.key] = change
                delta2[port.key] = self.balance(change.rx, change.tx)

        # Thesis Delta1 compares traffic arriving on a switch link with the
        # corresponding count reported by its neighbour.  For a veth/router
        # path, local transit RX and the peer transit TX are those quantities.
        delta1: dict[str, float] = {}
        for switch in self.switches.values():
            c21_c22 = 0
            r2in = 0
            for port in switch.ports:
                if port.kind != "transit":
                    continue
                c21_c22 += deltas[port.key].rx
                peer = self._find_port(port.peer_switch, port.peer_port)
                r2in += deltas[peer.key].tx
            delta1[switch.name] = self.balance(c21_c22, r2in)
        return deltas, delta1, delta2

    def _find_port(self, switch_name: str | None, port_name: str | None) -> Port:
        switch = self.switches[switch_name or ""]
        candidates = [port for port in switch.ports if port.name == port_name]
        if len(candidates) != 1:
            raise ValueError(f"cannot resolve peer {switch_name}/{port_name}")
        return candidates[0]

    def evaluate(self, deltas: dict[str, Counter], delta1: dict[str, float],
                 delta2: dict[str, float], timestamp: float | None = None) -> list[dict]:
        timestamp = time.time() if timestamp is None else timestamp
        suspicious_switches = [
            switch for switch in self.switches.values()
            if delta1[switch.name] > self.DELTA1_STANDARD
        ]
        suspicious_switches.sort(key=lambda switch: delta1[switch.name], reverse=True)
        scope = suspicious_switches or list(self.switches.values())
        events: list[dict] = []

        candidates: list[tuple[float, Switch, Port]] = []
        for switch in scope:
            for port in switch.ports:
                count = deltas[port.key]
                score = delta2[port.key]
                enough = count.rx + count.tx >= self.minimum_packets
                streak_key = f"{switch.name}/{port.name}"
                self.streaks[streak_key] = self.streaks.get(streak_key, 0) + 1 \
                    if enough and score > self.DELTA2_PORT else 0
                if self.streaks[streak_key] >= self.confirmation_windows:
                    candidates.append((score, switch, port))

        # Edge sources are the terminal condition of traceback.  Handle them
        # first so an upstream transit meter is unnecessary when the edge is
        # visible in the controller's topology.
        candidates.sort(key=lambda item: (item[2].kind != "edge", -item[0]))
        blocked_edge = False
        for score, switch, port in candidates:
            action = "observe"
            detail = ""
            try:
                if port.kind == "edge":
                    if port.trusted:
                        action, detail = "administrator_action", "trusted edge was not auto-blocked"
                    elif self.actuator.block(port):
                        action, blocked_edge = "block", True
                    else:
                        action, blocked_edge = "already_blocked", True
                elif not blocked_edge:
                    if delta1[switch.name] > self.DELTA1_STRICT or not suspicious_switches:
                        action = "meter" if self.actuator.meter(port) else "already_metered"
                    detail = f"traceback={port.peer_switch}/{port.peer_port}"
                else:
                    action, detail = "traceback", f"source already blocked; peer={port.peer_switch}/{port.peer_port}"
            except (OSError, subprocess.CalledProcessError, RuntimeError) as exc:
                action, detail = "action_error", str(exc)

            event = {
                "timestamp": timestamp,
                "switch": switch.name,
                "subnet": switch.subnet,
                "delta1": delta1[switch.name],
                "port": port.name,
                "interface": port.interface,
                "kind": port.kind,
                "rx": deltas[port.key].rx,
                "tx": deltas[port.key].tx,
                "delta2": score,
                "action": action,
                "detail": detail,
            }
            events.append(event)
            self.event_sink(event)

        recorded = {(event["switch"], event["port"]) for event in events}
        for switch in self.switches.values():
            for port in switch.ports:
                if (switch.name, port.name) in recorded:
                    continue
                count = deltas[port.key]
                event = {
                    "timestamp": timestamp,
                    "switch": switch.name,
                    "subnet": switch.subnet,
                    "delta1": delta1[switch.name],
                    "port": port.name,
                    "interface": port.interface,
                    "kind": port.kind,
                    "rx": count.rx,
                    "tx": count.tx,
                    "delta2": delta2[port.key],
                    "action": "observe",
                    "detail": "",
                }
                events.append(event)
                self.event_sink(event)
        return events


def load_topology(path: Path) -> list[Switch]:
    data = json.loads(path.read_text(encoding="utf-8"))
    switches = []
    for raw_switch in data.get("switches", []):
        ports = [Port(**raw_port) for raw_port in raw_switch.get("ports", [])]
        switches.append(Switch(raw_switch["name"], raw_switch["subnet"], ports))
    return switches


def main() -> int:
    parser = argparse.ArgumentParser(description="RAPID network segmentation controller")
    parser.add_argument("--topology", type=Path, default=Path("config/rapid_topology.json"))
    parser.add_argument("--interval", type=float, default=1.0)
    parser.add_argument("--duration", type=float, default=0, help="0 runs until interrupted")
    parser.add_argument("--minimum-packets", type=int, default=50)
    parser.add_argument("--confirmation-windows", type=int, default=2)
    parser.add_argument("--csv-out", type=Path)
    parser.add_argument("--events-out", type=Path)
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--cleanup-on-exit", action="store_true")
    parser.add_argument("--once", action="store_true")
    args = parser.parse_args()

    fields = ["timestamp", "switch", "subnet", "delta1", "port", "interface",
              "kind", "rx", "tx", "delta2", "action", "detail"]
    csv_file = None
    writer = None
    events: list[dict] = []
    if args.csv_out:
        args.csv_out.parent.mkdir(parents=True, exist_ok=True)
        csv_file = args.csv_out.open("w", newline="", encoding="utf-8")
        writer = csv.DictWriter(csv_file, fieldnames=fields)
        writer.writeheader()

    def sink(event: dict) -> None:
        events.append(event)
        if writer:
            writer.writerow(event)
            csv_file.flush()
        print("[RAPID] {switch}/{port} D1={delta1:.3f} D2={delta2:.3f} -> {action}".format(**event), flush=True)

    actuator = Actuator(dry_run=args.dry_run)
    controller = RapidController(load_topology(args.topology), actuator=actuator,
                                 minimum_packets=args.minimum_packets,
                                 confirmation_windows=args.confirmation_windows,
                                 event_sink=sink)
    stopping = False

    def stop(_signum, _frame):
        nonlocal stopping
        stopping = True

    signal.signal(signal.SIGINT, stop)
    signal.signal(signal.SIGTERM, stop)
    started = time.monotonic()
    try:
        controller.prime()
        while not stopping:
            time.sleep(args.interval)
            controller.evaluate(*controller.sample())
            if args.once or (args.duration and time.monotonic() - started >= args.duration):
                break
    finally:
        if args.events_out:
            args.events_out.parent.mkdir(parents=True, exist_ok=True)
            args.events_out.write_text(json.dumps(events, indent=2), encoding="utf-8")
        if csv_file:
            csv_file.close()
        if args.cleanup_on_exit:
            actuator.cleanup()
    return 0


if __name__ == "__main__":
    sys.exit(main())
