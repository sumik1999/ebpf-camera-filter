#!/usr/bin/env python3
"""plot.py -- every thesis/slide figure, generated from recorded CSV files.

  python3 eval/plot.py eval/raw/<date>_<commit>_sim

Writes PNGs to eval/figures/. Each figure's footer names the CSV it came
from and the commit, so every number can be traced back.

Inputs:
  <run>/measure.csv, functional.csv, flood_model.csv, xc_check.txt  (bench/run_sim.py)
  eval/raw/*_96f85bd_qemu_insns-O2/bench.csv  (interpreter before the memset fix)
  eval/raw/*_c8c8139_qemu_insns-O2/bench.csv  (after)
"""
import csv
import glob
import os
import re
import statistics
import sys

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import flood_model  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FIG = os.path.join(ROOT, "eval", "figures")

# Reference categorical palette (dataviz skill, light mode), fixed order.
BLUE, ORANGE, AQUA, YELLOW, MAGENTA = "#2a78d6", "#eb6834", "#1baf7a", "#eda100", "#e87ba4"
INK, INK2, GRID = "#1f1f1e", "#5f5e57", "#e4e3dc"
CFG_LABEL = {"C0": "C0\nno filter", "C1": "C1\nnative C\nlwIP hook", "C2": "C2\nVM\nlwIP hook",
             "C3": "C3\nnative C\ndriver hook", "C4": "C4\nVM\ndriver hook"}
CONFIGS = ["C0", "C1", "C2", "C3", "C4"]

plt.rcParams.update({
    "font.family": "DejaVu Sans", "font.size": 10, "axes.edgecolor": INK2, "axes.labelcolor": INK,
    "xtick.color": INK2, "ytick.color": INK2, "axes.spines.top": False, "axes.spines.right": False,
    "axes.grid": True, "grid.color": GRID, "grid.linewidth": 0.8, "axes.axisbelow": True,
    "legend.frameon": False, "figure.dpi": 100, "savefig.dpi": 200,
})


def footer(fig, text):
    fig.text(0.01, 0.01, text, fontsize=7, color=INK2, ha="left", va="bottom")


def save(fig, name):
    os.makedirs(FIG, exist_ok=True)
    path = os.path.join(FIG, name)
    fig.savefig(path, bbox_inches="tight", facecolor="white")
    plt.close(fig)
    print("wrote", os.path.relpath(path, ROOT))


def bar_labels(ax, bars, fmt="{:.0f}"):
    for b in bars:
        ax.annotate(fmt.format(b.get_height()), (b.get_x() + b.get_width() / 2, b.get_height()),
                    xytext=(0, 2), textcoords="offset points", ha="center", va="bottom", fontsize=8, color=INK)


def medians(run, build):
    v = {}
    with open(os.path.join(run, "measure.csv")) as f:
        for r in csv.DictReader(f):
            if r["build"] == build:
                v.setdefault((r["config"], r["kind"]), []).append(int(r["insns"]))
    return {k: statistics.median(x) for k, x in v.items()}


def fig_per_frame(run, src):
    """Attack vs legit echo, per config, FreeRTOS build."""
    m = medians(run, "rtos")
    fig, ax = plt.subplots(figsize=(8, 4.2))
    x = range(len(CONFIGS))
    w = 0.38
    b1 = ax.bar([i - w / 2 - 0.01 for i in x], [m[(c, "attack")] for c in CONFIGS], w, color=ORANGE,
                label="attack frame (UDP to port 1900)")
    b2 = ax.bar([i + w / 2 + 0.01 for i in x], [m[(c, "echo")] for c in CONFIGS], w, color=BLUE,
                label="legit frame (UDP echo, port 7)")
    bar_labels(ax, b1)
    bar_labels(ax, b2)
    ax.set_xticks(list(x), [CFG_LABEL[c] for c in CONFIGS], fontsize=8.5)
    ax.set_ylabel("instructions per frame (whole receive path)")
    ax.set_title("Cost of one frame under FreeRTOS: early filtering halves the attack cost",
                 loc="left", fontsize=11, color=INK)
    ax.set_ylim(0, ax.get_ylim()[1] * 1.22)
    ax.legend(loc="upper center", ncol=2, fontsize=8.5)
    footer(fig, f"Emulated Cortex-M33 (QEMU mps2-an505), FreeRTOS 11.2.0 + lwIP 2.2.1, -O2. Interrupt, task switches "
                f"and mailbox included. Median of 3 frames.\nSource: {src}/measure.csv. Instruction counts are a "
                f"lower bound on cycles.")
    fig.subplots_adjust(bottom=0.3)
    save(fig, "fig1_per_frame_cost.png")


def fig_rtos_vs_nosys(run, src):
    """Attack frame: with and without the RTOS -- why placement matters more under an RTOS."""
    r, n = medians(run, "rtos"), medians(run, "nosys")
    fig, ax = plt.subplots(figsize=(8, 4))
    x = range(len(CONFIGS))
    w = 0.38
    b1 = ax.bar([i - w / 2 - 0.01 for i in x], [n[(c, "attack")] for c in CONFIGS], w, color=AQUA,
                label="without RTOS (lwIP called directly)")
    b2 = ax.bar([i + w / 2 + 0.01 for i in x], [r[(c, "attack")] for c in CONFIGS], w, color=BLUE,
                label="with FreeRTOS (interrupt, RX task, mailbox, tcpip_thread)")
    bar_labels(ax, b1)
    bar_labels(ax, b2)
    ax.set_xticks(list(x), [CFG_LABEL[c] for c in CONFIGS], fontsize=8.5)
    ax.set_ylabel("instructions per attack frame")
    ax.set_title("A late drop still pays for the mailbox hand-off and a task switch",
                 loc="left", fontsize=11, color=INK)
    ax.set_ylim(0, ax.get_ylim()[1] * 1.22)
    ax.legend(loc="upper center", ncol=2, fontsize=8.5)
    footer(fig, f"Emulated Cortex-M33 (QEMU), -O2, median of 3 frames. Source: {src}/measure.csv.")
    fig.subplots_adjust(bottom=0.3)
    save(fig, "fig2_rtos_vs_nosys_attack.png")


def fig_flood(run, src):
    """MODEL: share of legit traffic that survives vs attack rate."""
    c = flood_model.costs(run)
    fig, ax = plt.subplots(figsize=(8, 4.2))
    rates = [1000 * (1.05 ** i) for i in range(0, 120)]
    colors = {"C0": INK2, "C1": YELLOW, "C2": MAGENTA, "C3": AQUA, "C4": BLUE}
    for cfg in CONFIGS:
        ca, ce = c[(cfg, "attack")], c[(cfg, "echo")]
        ys = [100 * flood_model.survival(ca, ce, r) for r in rates]
        ax.plot(rates, ys, color=colors[cfg], linewidth=2, label=f"{cfg} {flood_model.NAMES[cfg]}")
    for label, pps in flood_model.LINE_RATE.items():
        ax.axvline(pps, color=INK2, linewidth=1, linestyle=(0, (3, 3)))
        ax.text(pps * 1.04, 8, f"{label}\nline rate", fontsize=8, color=INK2)
    ax.set_xscale("log")
    ax.set_ylim(0, 105)
    ax.set_xlabel("attack rate (frames per second, log scale)")
    ax.set_ylabel("legit traffic delivered (%)")
    ax.set_title("MODEL: legit traffic kept during a UDP flood (250 MHz, 1 cycle per instruction)",
                 loc="left", fontsize=11, color=INK)
    ax.legend(loc="lower left", fontsize=8)
    footer(fig, "MODEL built on measured instruction counts (FreeRTOS build); assumptions in eval/flood_model.py. "
                "1 cycle/instruction is optimistic:\nreal capacity is lower (at 2 cycles/instruction C4 handles "
                f"about half the rate). Source: {src}/measure.csv.")
    fig.subplots_adjust(bottom=0.22)
    save(fig, "fig3_flood_model.png")


def fig_d5(run, src):
    """D5: ST's RX loop vs the fixed loop, C4, mixed flood, FreeRTOS build."""
    rows = [r for r in csv.DictReader(open(os.path.join(run, "functional.csv")))
            if r["build"] == "rtos" and r["trace"] == "mix90" and r["cfg"] == "4"]
    requests = int(next(r for r in rows if r["loop"] == "fixed" and r["burst"] == "1")["echo_rx"])
    fig, (a1, a2) = plt.subplots(1, 2, figsize=(9, 3.8))
    for loop, color, name in (("st", ORANGE, "ST's original RX loop"), ("fixed", BLUE, "fixed RX loop (D5)")):
        rr = sorted((r for r in rows if r["loop"] == loop), key=lambda r: int(r["burst"]))
        b = [int(r["burst"]) for r in rr]
        a1.plot(b, [100 * int(r["tx_echo"]) / requests for r in rr], color=color, linewidth=2, marker="o",
                markersize=6, label=name)
        a2.plot(b, [100 * int(r["mac_missed"]) / int(r["frames_in"]) for r in rr], color=color, linewidth=2,
                marker="o", markersize=6, label=name)
    for a in (a1, a2):
        a.set_xticks([1, 2, 4, 8])
        a.set_xlabel("frames arriving per RX-task wake-up")
    a1.set_ylabel("legit echo requests answered (%)")
    a2.set_ylabel("frames lost at the MAC, ring full (%)")
    a1.set_ylim(0, 105)
    a2.set_ylim(0, 105)
    a1.legend(loc="lower left", fontsize=8)
    fig.suptitle("D5: with ST's loop, early drops leave frames stranded and the 4-slot ring overflows",
                 x=0.01, ha="left", fontsize=11, color=INK)
    footer(fig, f"Config C4 (VM at driver hook), trace mix90 (90 % attack), FreeRTOS build. 8 per wake-up exceeds "
                f"the 4 descriptors for both loops.\nSource: {src}/functional.csv.")
    fig.subplots_adjust(bottom=0.25, wspace=0.3)
    save(fig, "fig4_d5_rx_loop.png")


def fig_memset():
    """Interpreter optimisation: before vs after the memset fix (Day 1 QEMU bench)."""
    before = glob.glob(os.path.join(ROOT, "eval", "raw", "*_96f85bd_qemu_insns-O2", "bench.csv"))[0]
    after = glob.glob(os.path.join(ROOT, "eval", "raw", "*_c8c8139_qemu_insns-O2", "bench.csv"))[0]
    rd = lambda p: {r["case"]: int(r["insns_net"]) for r in csv.DictReader(open(p))}
    b, a = rd(before), rd(after)
    cases = [("filter_vm_eth_attack", "attack\n(7 bytecode instr.)"), ("filter_vm_eth_tcp", "TCP\n(5)"),
             ("filter_vm_eth_arp", "ARP\n(3)"), ("filter_native_eth_attack", "native C\nattack")]
    fig, ax = plt.subplots(figsize=(7, 3.8))
    x = range(len(cases))
    w = 0.38
    b1 = ax.bar([i - w / 2 - 0.01 for i in x], [b[c] for c, _ in cases], w, color=INK2, label="before (memset)")
    b2 = ax.bar([i + w / 2 + 0.01 for i in x], [a[c] for c, _ in cases], w, color=BLUE, label="after (8 stores)")
    bar_labels(ax, b1)
    bar_labels(ax, b2)
    ax.set_xticks(list(x), [n for _, n in cases], fontsize=8.5)
    ax.set_ylabel("instructions per call")
    ax.set_title("Interpreter optimisation found with the emulator: -39 % on the attack path",
                 loc="left", fontsize=11, color=INK)
    ax.set_ylim(0, ax.get_ylim()[1] * 1.15)
    ax.legend(loc="upper right", fontsize=8.5)
    footer(fig, f"Emulated Cortex-M33, -O2, filter call only. Sources: {os.path.relpath(before, ROOT)}, "
                f"{os.path.relpath(after, ROOT)}.")
    fig.subplots_adjust(bottom=0.22)
    save(fig, "fig5_memset_fix.png")


def fig_footprint(run, src):
    """Code size of each component (arm-none-eabi-gcc 15.2.1, -O2, .text)."""
    sizes = {}
    for line in open(os.path.join(run, "xc_check.txt")):
        m = re.match(r"\s*(\d+)\s+(\d+)\s+(\d+)\s+\d+\s+\w+\s+(\S+)\.o$", line)
        if m:
            sizes[m.group(4)] = (int(m.group(1)), int(m.group(2)) + int(m.group(3)))
    parts = [("vm_interpreter", "interpreter"), ("vm_verifier", "verifier"), ("vm_slot", "slot"),
             ("vm_loader", "loader"), ("xdp_dma_hook", "driver hook"), ("lwip_ip4_hook", "lwIP hook core"),
             ("ddos_native", "native C filter")]
    parts = [(k, n) for k, n in parts if k in sizes]
    fig, ax = plt.subplots(figsize=(7.5, 3.6))
    vals = [sizes[k][0] for k, _ in parts]
    bars = ax.barh([n for _, n in parts], vals, color=[BLUE] * 4 + [AQUA] * 2 + [ORANGE] * (len(parts) - 6),
                   height=0.6)
    for bar, v in zip(bars, vals):
        ax.annotate(f"{v} B", (v, bar.get_y() + bar.get_height() / 2), xytext=(3, 0), textcoords="offset points",
                    va="center", fontsize=8, color=INK)
    ax.invert_yaxis()
    ax.set_xlabel("code size (.text bytes)")
    ax.grid(axis="y", visible=False)
    vm_total = sum(sizes[k][0] for k in ("vm_interpreter", "vm_verifier", "vm_slot", "vm_loader") if k in sizes)
    ax.set_title(f"Code footprint: VM engine {vm_total:,} B in total, 0 B static RAM; 2 MB flash on the chip",
                 loc="left", fontsize=10.5, color=INK)
    footer(fig, f"arm-none-eabi-gcc 15.2.1, Cortex-M33, -O2. Loader adds 2 x 4 KB RAM buffers per hook. "
                f"Source: {src}/xc_check.txt.")
    fig.subplots_adjust(bottom=0.2)
    save(fig, "fig6_footprint.png")


def main():
    run = sys.argv[1].rstrip("/")
    src = os.path.relpath(run, ROOT)
    fig_per_frame(run, src)
    fig_rtos_vs_nosys(run, src)
    fig_flood(run, src)
    fig_d5(run, src)
    fig_memset()
    if os.path.exists(os.path.join(run, "xc_check.txt")):
        fig_footprint(run, src)


if __name__ == "__main__":
    main()
