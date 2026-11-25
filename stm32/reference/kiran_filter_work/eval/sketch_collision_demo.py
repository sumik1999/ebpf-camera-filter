#!/usr/bin/env python3
import csv
import zlib
from pathlib import Path


WIDTH = 16
DEPTH = 4
HEAVY_THRESHOLD = 200
OUT_CSV = Path("eval/results/sketch_collision_demo.csv")
OUT_PNG = Path("eval/results/sketch_collision_demo.png")


def stable_hash(text, seed):
    if text in ("attacker_heavy", "apoc_telemetry_collision"):
        return seed
    return zlib.crc32(f"{seed}:{text}".encode("utf-8")) & 0xFFFFFFFF


def make_flows():
    flows = {"attacker_heavy": 1000}
    flows["apoc_telemetry_collision"] = 10
    for idx in range(1, 9):
        flows[f"apoc_telemetry_{idx}"] = 10
    return flows


def cms_estimates(flows):
    sketch = [[0 for _ in range(WIDTH)] for _ in range(DEPTH)]
    for flow, count in flows.items():
        for depth in range(DEPTH):
            sketch[depth][stable_hash(flow, depth) % WIDTH] += count
    estimates = {}
    for flow in flows:
        estimates[flow] = min(sketch[depth][stable_hash(flow, depth) % WIDTH] for depth in range(DEPTH))
    return estimates


def elastic_estimates(flows):
    heavy = {}
    light_flows = {}
    for flow, count in flows.items():
        if count >= HEAVY_THRESHOLD:
            heavy[flow] = count
        else:
            light_flows[flow] = count
    light_est = cms_estimates(light_flows)
    estimates = {}
    for flow in flows:
        if flow in heavy:
            estimates[flow] = heavy[flow]
        else:
            estimates[flow] = light_est.get(flow, 0)
    return estimates, heavy


def write_plot(rows):
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except Exception:
        return False
    labels = [row["flow"] for row in rows]
    true_counts = [int(row["true_count"]) for row in rows]
    cms = [int(row["cms_estimate"]) for row in rows]
    elastic = [int(row["elastic_estimate"]) for row in rows]
    x = range(len(labels))
    plt.figure(figsize=(10, 4.8))
    plt.plot(x, true_counts, marker="o", label="true")
    plt.plot(x, cms, marker="x", label="cms estimate")
    plt.plot(x, elastic, marker="s", label="elastic-style estimate")
    plt.yscale("log")
    plt.xticks(list(x), labels, rotation=35, ha="right")
    plt.ylabel("packet count, log scale")
    plt.legend()
    plt.tight_layout()
    plt.savefig(OUT_PNG)
    plt.close()
    return True


def main():
    flows = make_flows()
    cms = cms_estimates(flows)
    elastic, heavy = elastic_estimates(flows)
    rows = []
    for flow, true_count in flows.items():
        rows.append({
            "flow": flow,
            "true_count": true_count,
            "cms_estimate": cms[flow],
            "cms_overestimate": cms[flow] - true_count,
            "elastic_estimate": elastic[flow],
            "elastic_overestimate": elastic[flow] - true_count,
            "elastic_bucket": "heavy" if flow in heavy else "light",
        })

    OUT_CSV.parent.mkdir(parents=True, exist_ok=True)
    with OUT_CSV.open("w", encoding="utf-8", newline="") as fh:
        writer = csv.DictWriter(fh, fieldnames=list(rows[0].keys()))
        writer.writeheader()
        writer.writerows(rows)

    print("flow,true_count,cms_estimate,cms_overestimate,elastic_estimate,elastic_overestimate,elastic_bucket")
    for row in rows:
        print(",".join(str(row[key]) for key in row))
    print(f"csv={OUT_CSV}")
    if write_plot(rows):
        print(f"png={OUT_PNG}")
    else:
        print("png=not generated; matplotlib unavailable")


if __name__ == "__main__":
    main()
