#!/usr/bin/env python3
"""count_insns.py -- instructions executed between sim_mark_begin() and
sim_mark_end(), from a QEMU exec trace.

QEMU is run with `-accel tcg,one-insn-per-tb=on -d exec,nochain`, so the
trace has one line per executed guest instruction:

    Trace 0: 0x... [00000000/10000abc/...] symbol

The PC is the second field inside the brackets. A measurement window is
every line after the PC reaches sim_mark_begin (its only instruction,
`bx lr`) up to, not including, the line where the PC reaches
sim_mark_end. The N-th window belongs to the N-th "CASE <name> <verdict>"
line the program printed.

net = raw - raw(calib_empty), i.e. the marker overhead is removed.
The script checks that calib_nop10 measures exactly 10, otherwise it
fails: then the method is broken and no number should be used.

Usage: count_insns.py <elf> <trace> <stdout> <opt-level>   (CSV on stdout)
"""
import os
import re
import subprocess
import sys

ARM_BIN = os.environ.get("ARM_BIN", "/Applications/ArmGNUToolchain/15.2.rel1/arm-none-eabi/bin")


def symbol_addr(elf, name):
    out = subprocess.run([f"{ARM_BIN}/arm-none-eabi-nm", elf], capture_output=True, text=True, check=True).stdout
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[2] == name:
            return int(parts[0], 16) & ~1     # Thumb symbols have bit 0 set
    sys.exit(f"symbol {name} not found in {elf}")


def windows(trace, begin, end):
    pc_re = re.compile(r"^Trace \d+: \S+ \[[0-9a-f]+/([0-9a-f]+)/")
    counts, n, inside = [], 0, False
    with open(trace) as f:
        for line in f:
            m = pc_re.match(line)
            if not m:
                continue
            pc = int(m.group(1), 16)
            if pc == begin:
                inside, n = True, 0
            elif pc == end and inside:
                counts.append(n)
                inside = False
            elif inside:
                n += 1
    return counts


def main():
    elf, trace, stdout, opt = sys.argv[1:5]
    cases = []
    with open(stdout) as f:
        for line in f:
            if line.startswith("CASE "):
                _, name, verdict = line.split()
                cases.append((name, int(verdict)))
    counts = windows(trace, symbol_addr(elf, "sim_mark_begin"), symbol_addr(elf, "sim_mark_end"))
    if len(counts) != len(cases):
        sys.exit(f"{len(counts)} trace windows but {len(cases)} CASE lines")

    raw = dict(zip([c[0] for c in cases], counts))
    base = raw["calib_empty"]
    if raw["calib_nop10"] - base != 10:
        sys.exit(f"calibration failed: calib_nop10 measured {raw['calib_nop10'] - base}, expected 10")

    gcc = subprocess.run([f"{ARM_BIN}/arm-none-eabi-gcc", "-dumpversion"], capture_output=True, text=True).stdout.strip()
    try:
        commit = subprocess.run(["git", "rev-parse", "--short", "HEAD"], capture_output=True, text=True).stdout.strip()
        dirty = subprocess.run(["git", "status", "--porcelain", "--untracked-files=no"],
                               capture_output=True, text=True).stdout.strip()
        commit += "-dirty" if dirty else ""
    except OSError:
        commit = "unknown"

    print("case,verdict,insns_net,insns_raw,opt,gcc,commit")
    for (name, verdict), n in zip(cases, counts):
        print(f"{name},{'PASS' if verdict == 1 else 'DROP'},{n - base},{n},{opt},{gcc},{commit}")


if __name__ == "__main__":
    main()
