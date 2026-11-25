#!/usr/bin/env python3
"""Plot real RAPID Delta1/Delta2 decisions from rapid_metrics.csv."""

import argparse
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import pandas as pd


def plot_metrics(csv_path: Path, output_dir: Path) -> list[Path]:
    data = pd.read_csv(csv_path)
    required = {"timestamp", "switch", "port", "delta1", "delta2", "action"}
    missing = required - set(data.columns)
    if missing:
        raise ValueError(f"missing RAPID columns: {', '.join(sorted(missing))}")
    output_dir.mkdir(parents=True, exist_ok=True)
    if data.empty:
        return []

    data = data.copy()
    data["time"] = data["timestamp"] - data["timestamp"].min()
    outputs = []

    fig, axes = plt.subplots(2, 1, figsize=(12, 8), sharex=True)
    for name, group in data.groupby("switch"):
        axes[0].plot(group["time"], group["delta1"], marker="o", label=name)
    axes[0].axhline(0.15, color="orange", linestyle="--", label="standard 0.15")
    axes[0].axhline(0.33, color="red", linestyle="--", label="strict 0.33")
    axes[0].set_ylabel("Delta1")
    axes[0].set_title("RAPID link-balance metric")
    axes[0].legend()
    axes[0].grid(alpha=0.3)

    for (switch, port), group in data.groupby(["switch", "port"]):
        axes[1].plot(group["time"], group["delta2"], marker="o", label=f"{switch}/{port}")
    axes[1].axhline(0.5, color="red", linestyle="--", label="port threshold 0.5")
    axes[1].set_xlabel("Time (seconds)")
    axes[1].set_ylabel("Delta2")
    axes[1].set_title("RAPID signed port-balance metric")
    axes[1].legend(fontsize="small", ncol=2)
    axes[1].grid(alpha=0.3)
    fig.tight_layout()
    metric_path = output_dir / "rapid_delta_metrics.png"
    fig.savefig(metric_path, dpi=150)
    plt.close(fig)
    outputs.append(metric_path)

    action_data = data[data["action"] != "observe"]
    fig, ax = plt.subplots(figsize=(12, 5))
    labels = sorted({f"{row.switch}/{row.port}" for row in action_data.itertuples()})
    positions = {label: index for index, label in enumerate(labels)}
    colors = {"block": "red", "meter": "orange", "traceback": "blue",
              "administrator_action": "purple", "action_error": "black"}
    for row in action_data.itertuples():
        label = f"{row.switch}/{row.port}"
        ax.scatter(row.time, positions[label], color=colors.get(row.action, "gray"), s=80)
        ax.annotate(row.action, (row.time, positions[label]), xytext=(4, 4),
                    textcoords="offset points", fontsize="small")
    ax.set_yticks(range(len(labels)), labels)
    ax.set_xlabel("Time (seconds)")
    ax.set_title("RAPID mitigation and traceback decisions")
    ax.grid(axis="x", alpha=0.3)
    fig.tight_layout()
    action_path = output_dir / "rapid_actions.png"
    fig.savefig(action_path, dpi=150)
    plt.close(fig)
    outputs.append(action_path)
    return outputs


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--csv", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path)
    args = parser.parse_args()
    output = args.output_dir or args.csv.parent
    for path in plot_metrics(args.csv, output):
        print(path)


if __name__ == "__main__":
    main()
