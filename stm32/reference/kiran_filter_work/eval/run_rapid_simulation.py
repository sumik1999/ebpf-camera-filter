#!/usr/bin/env python3
"""Deterministic, non-root simulation of the RAPID port algorithm."""

import argparse
import csv
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from rapid_adaptive_controller import Actuator, Counter, Port, RapidController, Switch
from visualize_rapid_metrics import plot_metrics


def simulated_topology():
    return [
        Switch("terminal-switch", "10.10.0.0/24", [
            Port("apoc", "apoc0", "edge", trusted=True),
            Port("attacker", "attack0", "edge"),
            Port("guard", "term0", "transit", peer_switch="camera-switch", peer_port="guard"),
        ]),
        Switch("camera-switch", "10.20.0.0/24", [
            Port("guard", "cam0", "transit", peer_switch="terminal-switch", peer_port="guard"),
            Port("camera1", "camera1", "edge", trusted=True),
        ]),
    ]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--duration", type=int, default=30)
    parser.add_argument("--attack-start", type=int, default=8)
    parser.add_argument("--attack-end", type=int, default=20)
    parser.add_argument("--pps-attack", type=int, default=2000)
    parser.add_argument("--output", type=Path, default=Path("eval/results/test_rapid"))
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)

    rows = []
    controller = RapidController(simulated_topology(), actuator=Actuator(dry_run=True),
                                 minimum_packets=50, confirmation_windows=2,
                                 event_sink=rows.append)
    for second in range(args.duration):
        attack = args.pps_attack if args.attack_start <= second < args.attack_end else 0
        normal = 200
        deltas = {
            "root:apoc0": Counter(normal, normal),
            "root:attack0": Counter(attack, 5 if attack else 0),
            "root:term0": Counter(normal, normal + attack),
            "root:cam0": Counter(normal + attack, normal),
            "root:camera1": Counter(normal + attack, normal + attack),
        }
        delta2 = {key: controller.balance(value.rx, value.tx) for key, value in deltas.items()}
        # Paired transit counters match in this healthy simulated datapath.
        delta1 = {"terminal-switch": 0.0, "camera-switch": 0.0}
        controller.evaluate(deltas, delta1, delta2, timestamp=float(second))

    fields = ["timestamp", "switch", "subnet", "delta1", "port", "interface",
              "kind", "rx", "tx", "delta2", "action", "detail"]
    csv_path = args.output / "rapid_metrics.csv"
    with csv_path.open("w", newline="", encoding="utf-8") as output:
        writer = csv.DictWriter(output, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)
    print(csv_path)
    for path in plot_metrics(csv_path, args.output):
        print(path)


if __name__ == "__main__":
    main()
