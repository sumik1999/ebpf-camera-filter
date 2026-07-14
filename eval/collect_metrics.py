#!/usr/bin/env python3
import argparse
import json
import os
from pathlib import Path


def load_json(path):
    try:
        with open(path, "r", encoding="utf-8") as fh:
            return json.load(fh)
    except FileNotFoundError:
        return None


def main():
    parser = argparse.ArgumentParser(description="Collect one experiment's metrics into summary.json")
    parser.add_argument("--run-dir", required=True)
    parser.add_argument("--case", required=True)
    parser.add_argument("--attack-type", required=True)
    args = parser.parse_args()

    run_dir = Path(args.run_dir)
    telemetry = load_json(run_dir / "telemetry.json") or {}
    attack = load_json(run_dir / "attack.json") or {}
    cameras = {}
    for path in sorted(run_dir.glob("camera*.json")):
        cameras[path.stem] = load_json(path) or {}

    attack_received = 0
    tcp_connections = 0
    for camera in cameras.values():
        attack_received += int(camera.get("udp_attack_like_received", 0))
        tcp_connections += int(camera.get("tcp_connections", 0))

    attack_sent = int(attack.get("sent", 0))
    if args.attack_type == "udp":
        observed_delivered = attack_received
    else:
        observed_delivered = tcp_connections
    estimated_dropped = max(0, attack_sent - observed_delivered) if attack_sent else 0

    attach_status = ""
    attach_file = run_dir / "attach_status.txt"
    if attach_file.exists():
        attach_status = attach_file.read_text(encoding="utf-8", errors="replace")

    summary = {
        "case": args.case,
        "attack_type": args.attack_type,
        "target_mode": attack.get("target_mode", "unknown"),
        "attack_pps": attack.get("pps_target", 0),
        "telemetry": telemetry.get("total", {}),
        "per_camera": telemetry.get("per_camera", {}),
        "attack": attack,
        "camera_observed": {
            "udp_attack_packets": attack_received,
            "tcp_connections": tcp_connections,
        },
        "estimated_attack_dropped": estimated_dropped,
        "estimated_attack_drop_pct": (estimated_dropped / attack_sent * 100.0) if attack_sent else 0.0,
        "false_positive_estimate": telemetry.get("total", {}).get("loss", 0),
        "false_negative_estimate": observed_delivered,
        "ebpf_attach_status": attach_status,
        "notes": "CPU, softirq, and exact eBPF memory are environment-dependent; this prototype records app-level delivery and attach status.",
    }

    out = run_dir / "summary.json"
    with open(out, "w", encoding="utf-8") as fh:
        json.dump(summary, fh, indent=2, sort_keys=True)
    print(out)


if __name__ == "__main__":
    main()
