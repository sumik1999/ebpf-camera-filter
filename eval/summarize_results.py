#!/usr/bin/env python3
import argparse
import csv
import json
import re
import statistics
from pathlib import Path


def load(path):
    with open(path, "r", encoding="utf-8") as fh:
        return json.load(fh)


def parse_attach_status(text):
    if not text:
        return "unavailable"
    match = re.search(r"^mode=(.+)$", text, re.MULTILINE)
    return match.group(1).strip() if match else text.strip().splitlines()[-1][:80]


def infer_target_mode(run_id, data):
    if data.get("target_mode") and data.get("target_mode") != "unknown":
        return data["target_mode"]
    for mode in ("single_camera", "round_robin", "all_cameras"):
        if mode in run_id:
            return mode
    return data.get("attack", {}).get("target_mode", "unknown")


def average_metric(per_camera, key):
    values = []
    for item in per_camera.values():
        value = item.get(key)
        if value is not None:
            values.append(float(value))
    return round(statistics.fmean(values), 3) if values else ""


def main():
    parser = argparse.ArgumentParser(description="Summarize experiment result directories")
    parser.add_argument("--results-dir", default="eval/results")
    args = parser.parse_args()
    root = Path(args.results_dir)
    rows = []
    for summary_path in sorted(root.glob("*/summary.json")):
        data = load(summary_path)
        tel = data.get("telemetry", {})
        attack = data.get("attack", {})
        per_camera = data.get("per_camera", {})
        observed = data.get("camera_observed", {})
        observed_attack = observed.get("udp_attack_packets", 0) + observed.get("tcp_connections", 0)
        run_id = summary_path.parent.name
        rows.append({
            "run_id": run_id,
            "case": data.get("case"),
            "attack_type": data.get("attack_type"),
            "target_mode": infer_target_mode(run_id, data),
            "attack_pps": data.get("attack_pps", attack.get("pps_target", 0)),
            "telemetry_sent": tel.get("sent", 0),
            "telemetry_replies": tel.get("replies", 0),
            "telemetry_loss_pct": round(float(tel.get("loss_pct", 0.0)), 3),
            "avg_rtt_ms": average_metric(per_camera, "avg_rtt_ms"),
            "p95_rtt_ms": average_metric(per_camera, "p95_rtt_ms"),
            "jitter_ms": average_metric(per_camera, "jitter_ms"),
            "attack_sent": attack.get("sent", 0),
            "camera_observed_attack": observed_attack,
            "estimated_attack_dropped": data.get("estimated_attack_dropped", 0),
            "estimated_attack_drop_pct": round(float(data.get("estimated_attack_drop_pct", 0.0)), 3),
            "tc_attach_status": parse_attach_status(data.get("ebpf_attach_status", "")),
            "notes": data.get("notes", ""),
        })

    csv_path = root / "summary.csv"
    if rows:
        with open(csv_path, "w", encoding="utf-8", newline="") as fh:
            writer = csv.DictWriter(fh, fieldnames=list(rows[0].keys()))
            writer.writeheader()
            writer.writerows(rows)

    print("run_id,case,attack_type,target_mode,attack_pps,telemetry_loss_pct,attack_sent,estimated_drop_pct,tc_attach_status")
    for row in rows:
        print(f"{row['run_id']},{row['case']},{row['attack_type']},{row['target_mode']},{row['attack_pps']},{row['telemetry_loss_pct']},{row['attack_sent']},{row['estimated_attack_drop_pct']},{row['tc_attach_status']}")
    if rows:
        print(f"summary_csv={csv_path}")

    warnings = []
    if rows and all(float(row["telemetry_loss_pct"]) == 0.0 for row in rows):
        warnings.append("all summarized runs have 0% telemetry loss; attack may be too weak or WSL2 absorbed the load")
    by_shape = {}
    for row in rows:
        key = (row["attack_type"], row["target_mode"], row["attack_pps"])
        by_shape.setdefault(key, {})[row["case"]] = row["estimated_attack_drop_pct"]
    for key, cases in by_shape.items():
        if "cms_ebpf" in cases and "elastic_ebpf" in cases and cases["cms_ebpf"] == cases["elastic_ebpf"]:
            warnings.append(f"CMS and Elastic have same drop percentage for {key}: {cases['cms_ebpf']}%")
    for row in rows:
        if row["case"] in ("cms_ebpf", "elastic_ebpf") and row["tc_attach_status"] != "tc_ebpf_attached":
            warnings.append(f"eBPF attach may have failed in {row['run_id']}: {row['tc_attach_status']}")
    if rows:
        warnings.append("exact kernel drop counters are unavailable; app-level delivered-vs-sent estimates are used")
    for warning in dict.fromkeys(warnings):
        print(f"WARNING: {warning}")


if __name__ == "__main__":
    main()
