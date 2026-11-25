#!/usr/bin/env python3
"""flood_model.py -- a simple, clearly labelled MODEL of flood behaviour,
built on instruction counts MEASURED in the emulator.

Measured input (from a run directory made by bench/run_sim.py):
  measure.csv, FreeRTOS build: instructions per frame for each config,
  including the interrupt, task switches and the lwIP mailbox.

Model (these are assumptions, not measurements):
  - CPU: 250 MHz Cortex-M33 (STM32H563).
  - cycles per frame = CPI x instructions. CPI = 1 is a LOWER bound on
    cycles (real loads, branches and flash wait states cost more), so the
    capacities below are UPPER bounds. CPI = 2 is shown as a sensitivity.
  - legit traffic: 1000 UDP echo requests per second during the flood.
  - when the CPU is overloaded, frames are lost in the RX ring without
    regard to type, so the share of legit traffic that survives is
    capacity / demand.
Not modelled: interrupt latency, cache/flash effects, lwIP timers, TX cost.

Usage: flood_model.py <run_dir>   -> <run_dir>/flood_model.csv (+ printed table)
"""
import csv
import os
import statistics
import sys

F_CPU = 250e6
LEGIT_PPS = 1000
ATTACK_FRAME = 115                     # bytes, Ethernet frame without FCS (gen_traces.py SSDP flood)
WIRE_OVERHEAD = 4 + 8 + 12             # FCS + preamble/SFD + inter-frame gap
LINE_RATE = {f"{m} Mbit/s": m * 1e6 / ((ATTACK_FRAME + WIRE_OVERHEAD) * 8) for m in (10, 100)}
CONFIGS = ["C0", "C1", "C2", "C3", "C4"]
NAMES = {"C0": "no filter", "C1": "native C @ lwIP hook", "C2": "VM @ lwIP hook",
         "C3": "native C @ driver hook", "C4": "VM @ driver hook"}


def costs(run_dir, build="rtos"):
    """Median instructions per frame, per (config, kind)."""
    v = {}
    with open(os.path.join(run_dir, "measure.csv")) as f:
        for r in csv.DictReader(f):
            if r["build"] == build:
                v.setdefault((r["config"], r["kind"]), []).append(int(r["insns"]))
    return {k: statistics.median(x) for k, x in v.items()}


def demand(c_attack, c_echo, attack_pps, cpi):
    """CPU fraction needed (1.0 = 100 %)."""
    return (attack_pps * c_attack + LEGIT_PPS * c_echo) * cpi / F_CPU


def survival(c_attack, c_echo, attack_pps, cpi=1):
    d = demand(c_attack, c_echo, attack_pps, cpi)
    return 1.0 if d <= 1 else 1.0 / d


def max_attack_pps(c_attack, c_echo, cpi):
    return (F_CPU / cpi - LEGIT_PPS * c_echo) / c_attack


def main():
    run_dir = sys.argv[1]
    c = costs(run_dir)
    rows = []
    for cfg in CONFIGS:
        ca, ce = c[(cfg, "attack")], c[(cfg, "echo")]
        row = {"config": cfg, "name": NAMES[cfg], "insns_attack": ca, "insns_echo": ce,
               "max_attack_pps_cpi1": round(max_attack_pps(ca, ce, 1)),
               "max_attack_pps_cpi2": round(max_attack_pps(ca, ce, 2))}
        for label, pps in LINE_RATE.items():
            row[f"cpu_pct_at_{label.split()[0]}M_cpi1"] = round(100 * demand(ca, ce, pps, 1), 1)
            row[f"legit_survival_pct_at_{label.split()[0]}M_cpi1"] = round(100 * survival(ca, ce, pps), 1)
        rows.append(row)
    out = os.path.join(run_dir, "flood_model.csv")
    with open(out, "w", newline="") as f:
        f.write("# MODEL output (assumptions in eval/flood_model.py); inputs: measure.csv, build=rtos\n")
        w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        w.writeheader()
        w.writerows(rows)
    print(f"line rate for the {ATTACK_FRAME}-byte attack frame: " +
          ", ".join(f"{k} = {v:,.0f} frames/s" for k, v in LINE_RATE.items()))
    print(f"{'cfg':4s} {'attack':>7s} {'echo':>6s} {'max flood (CPI 1)':>18s} {'(CPI 2)':>9s} "
          f"{'CPU@10M':>8s} {'legit@100M':>11s}")
    for r in rows:
        print(f"{r['config']:4s} {r['insns_attack']:7.0f} {r['insns_echo']:6.0f} "
              f"{r['max_attack_pps_cpi1']:>15,d}/s {r['max_attack_pps_cpi2']:>7,d}/s "
              f"{r['cpu_pct_at_10M_cpi1']:7.1f}% {r['legit_survival_pct_at_100M_cpi1']:10.1f}%")
    print(f"saved {out}")


if __name__ == "__main__":
    main()
