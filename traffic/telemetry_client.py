#!/usr/bin/env python3
import argparse
import csv
import json
import os
import socket
import statistics
import time


def percentile(values, pct):
    if not values:
        return None
    ordered = sorted(values)
    idx = min(len(ordered) - 1, max(0, int(round((pct / 100.0) * (len(ordered) - 1)))))
    return ordered[idx]


def summarize_camera(samples, sent):
    replies = len(samples)
    losses = max(0, sent - replies)
    rtts = [sample["rtt_ms"] for sample in samples]
    return {
        "sent": sent,
        "replies": replies,
        "loss": losses,
        "loss_pct": (losses / sent * 100.0) if sent else 0.0,
        "avg_rtt_ms": statistics.fmean(rtts) if rtts else None,
        "p95_rtt_ms": percentile(rtts, 95),
        "jitter_ms": statistics.pstdev(rtts) if len(rtts) > 1 else 0.0,
        "timeout_count": losses,
    }


def main():
    parser = argparse.ArgumentParser(description="APOC telemetry probe client")
    parser.add_argument("--targets", required=True, help="comma-separated camera IPs")
    parser.add_argument("--port", type=int, default=5001)
    parser.add_argument("--interval-ms", type=int, default=50)
    parser.add_argument("--duration-sec", type=float, default=30)
    parser.add_argument("--timeout-ms", type=int, default=200)
    parser.add_argument("--out", required=True)
    parser.add_argument("--csv-out", default="")
    args = parser.parse_args()

    targets = [target.strip() for target in args.targets.split(",") if target.strip()]
    sent = {target: 0 for target in targets}
    samples = {target: [] for target in targets}
    csv_rows = []
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(args.timeout_ms / 1000.0)
    start = time.time()
    seq = 0

    while time.time() - start < args.duration_sec:
        loop_start = time.time()
        for target in targets:
            seq += 1
            payload = json.dumps({"seq": seq, "target": target, "sent_time": time.time()}).encode()
            sent[target] += 1
            t0 = time.perf_counter()
            try:
                sock.sendto(payload, (target, args.port))
                data, addr = sock.recvfrom(4096)
                if addr[0] == target:
                    rtt_ms = (time.perf_counter() - t0) * 1000.0
                    samples[target].append({
                        "seq": seq,
                        "rtt_ms": rtt_ms,
                        "reply": data.decode("utf-8", errors="replace")[:256],
                    })
                    csv_rows.append({
                        "time_sec": round(time.time() - start, 3),
                        "camera_ip": target,
                        "seq": seq,
                        "rtt_ms": round(rtt_ms, 3),
                        "status": "reply",
                    })
            except socket.timeout:
                csv_rows.append({
                    "time_sec": round(time.time() - start, 3),
                    "camera_ip": target,
                    "seq": seq,
                    "rtt_ms": "",
                    "status": "timeout",
                })
                pass
            except OSError:
                pass
        elapsed = time.time() - loop_start
        time.sleep(max(0.0, (args.interval_ms / 1000.0) - elapsed))

    per_camera = {target: summarize_camera(samples[target], sent[target]) for target in targets}
    total_sent = sum(item["sent"] for item in per_camera.values())
    total_replies = sum(item["replies"] for item in per_camera.values())
    total_loss = total_sent - total_replies
    result = {
        "started_at": start,
        "ended_at": time.time(),
        "duration_sec": args.duration_sec,
        "per_camera": per_camera,
        "total": {
            "sent": total_sent,
            "replies": total_replies,
            "loss": total_loss,
            "loss_pct": (total_loss / total_sent * 100.0) if total_sent else 0.0,
        },
    }
    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    with open(args.out, "w", encoding="utf-8") as fh:
        json.dump(result, fh, indent=2, sort_keys=True)
    if args.csv_out:
        os.makedirs(os.path.dirname(args.csv_out), exist_ok=True)
        with open(args.csv_out, "w", encoding="utf-8", newline="") as fh:
            writer = csv.DictWriter(fh, fieldnames=["time_sec", "camera_ip", "seq", "rtt_ms", "status"])
            writer.writeheader()
            writer.writerows(csv_rows)


if __name__ == "__main__":
    main()
