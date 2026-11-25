#!/usr/bin/env python3
"""run_sim.py -- the experiment runner. Everything runs on the emulated
Cortex-M33 (QEMU mps2-an505); results go to eval/raw/<date>_<commit>_sim/.
Two firmware builds are run: "nosys" (lwIP without an RTOS) and "rtos"
(FreeRTOS 11.2.0: real interrupt, RX task, mailbox, tcpip_thread). The rtos
build runs with -icount, so its clock and tick are deterministic.

1. functional.csv  every trace x config C0-C4 x RX loop (st|fixed) x burst
                   (1,2,4,8): frames in/missed, hook verdicts, frames
                   delivered to lwIP, replies sent, stranded frames (D5) ...
2. measure.csv     instructions per frame through the whole receive path
                   (RX task: driver -> early hook -> lwIP -> late hook ->
                   app -> reply), per config, for each frame kind
                   (QEMU exec trace, one instruction per block, calibrated)
3. demo.csv        live-update demo (C4): rulebooks sent to UDP port 9000
                   while traffic flows; checked against the trace's labels
4. footprint.txt   code/data size of the simulated firmware (arm-none-eabi-size)
5. meta.txt        date, commit, tool versions, trace SHA-256, commands

Run from the repo root, after committing (the commit hash must identify
the code):  python3 bench/run_sim.py
"""
import csv
import datetime
import hashlib
import io
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
QDIR = os.path.join(ROOT, "sim", "qemu_m33")
TRACES = os.path.join(ROOT, "sim", "eth_sim", "build", "traces")
ARM_BIN = os.environ.get("ARM_BIN", "/Applications/ArmGNUToolchain/15.2.rel1/arm-none-eabi/bin")
ELFS = {"nosys": os.path.join(QDIR, "build", "simfw.elf"),
        "rtos": os.path.join(QDIR, "build", "simrtos.elf")}

FUNC_TRACES = ["legit", "attack", "mix50", "mix90", "mix99", "odd"]
CONFIGS = [0, 1, 2, 3, 4]
LOOPS = ["st", "fixed"]
BURSTS = [1, 2, 4, 8]


def sh(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, text=True, check=True, **kw).stdout


def qemu(args, trace_log=None, build="nosys"):
    semi = "enable=on,target=native," + ",".join("arg=" + a for a in ["sim"] + args)
    cmd = ["qemu-system-arm", "-machine", "mps2-an505", "-nographic", "-no-reboot",
           "-semihosting-config", semi]
    if build == "rtos":
        cmd += ["-icount", "shift=0"]
    if trace_log:
        cmd += ["-accel", "tcg,one-insn-per-tb=on", "-d", "exec,nochain", "-D", trace_log]
    r = subprocess.run(cmd + ["-kernel", ELFS[build]], capture_output=True, text=True, cwd=QDIR)
    if r.returncode != 0:
        sys.exit(f"QEMU run failed ({r.returncode}): {' '.join(args)}\n{r.stdout}{r.stderr}")
    return r.stdout


def result_row(out):
    lines = [l for l in out.splitlines() if l.startswith("RESULT,")]
    header, data = lines[0].split(",")[1:], lines[1].split(",")[1:]
    return dict(zip(header, data))


def main():
    commit = sh(["git", "rev-parse", "--short", "HEAD"], cwd=ROOT).strip()
    dirty = sh(["git", "status", "--porcelain", "--untracked-files=no"], cwd=ROOT).strip()
    tag = commit + ("-dirty" if dirty else "")
    out_dir = os.path.join(ROOT, "eval", "raw", f"{datetime.date.today()}_{tag}_sim")
    os.makedirs(out_dir, exist_ok=True)

    sh(["python3", os.path.join(ROOT, "bench", "gen_traces.py"), TRACES])
    sh(["make", "-C", QDIR, "build/simfw.elf", "build/simrtos.elf"])

    # 1. functional matrix, both builds
    rows, stack_lines = [], []
    for build in ("nosys", "rtos"):
        for t in FUNC_TRACES:
            for c in CONFIGS:
                for l in LOOPS:
                    for b in BURSTS:
                        out = qemu([f"cfg={c}", f"loop={l}", f"burst={b}",
                                    f"trace=../eth_sim/build/traces/{t}.pcap"], build=build)
                        rows.append({"build": build, "trace": t, **result_row(out)})
                        stack_lines += [f"{t},C{c},{l},b{b},{x}" for x in out.splitlines() if x.startswith("RTOS,")]
    with open(os.path.join(out_dir, "rtos_stack.txt"), "w") as f:
        f.write("# smallest free stack (bytes) per FreeRTOS task, per run\n" + "\n".join(stack_lines) + "\n")
    with open(os.path.join(out_dir, "functional.csv"), "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        w.writeheader()
        w.writerows(rows)
    print(f"functional: {len(rows)} runs")

    # 2. per-frame instruction counts
    with open(os.path.join(TRACES, "measure.labels.csv")) as f:
        kinds = {int(r["index"]): r["kind"] for r in csv.DictReader(f)}
    mrows = []
    for build in ("nosys", "rtos"):
        for c in CONFIGS:
            log = os.path.join(QDIR, "build", f"measure_{build}_c{c}.trace")
            stdout_path = os.path.join(QDIR, "build", f"measure_{build}_c{c}.stdout")
            out = qemu([f"cfg={c}", "loop=fixed", "burst=1", "measure=1",
                        "trace=../eth_sim/build/traces/measure.pcap"], trace_log=log, build=build)
            with open(stdout_path, "w") as f:
                f.write(out)
            counted = sh(["python3", os.path.join(QDIR, "count_insns.py"), ELFS[build], log, stdout_path, "-O2"])
            for r in csv.DictReader(io.StringIO(counted)):
                if r["case"].startswith("f"):
                    i = int(r["case"][1:])
                    mrows.append({"build": build, "config": f"C{c}", "frame": i, "kind": kinds[i],
                                  "insns": r["insns_net"]})
            os.remove(log)                               # traces are large; the counts are kept
    with open(os.path.join(out_dir, "measure.csv"), "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=["build", "config", "frame", "kind", "insns"])
        w.writeheader()
        w.writerows(mrows)
    print(f"measure: {len(mrows)} frame counts")

    # 3. live-update demo, checked against the expected verdicts in the labels
    with open(os.path.join(TRACES, "loader_demo.labels.csv")) as f:
        labels = list(csv.DictReader(f))
    want_drops = sum(1 for l in labels if l["expected_with_filter"] == "DROP")
    want_echo = str(sum(l["kind"] == "echo" for l in labels))
    demo_rows, log_text, checks = [], "", {}
    for build in ("nosys", "rtos"):
        out = qemu(["cfg=4", "loop=fixed", "burst=1", "trace=../eth_sim/build/traces/loader_demo.pcap"], build=build)
        r = result_row(out)
        demo_rows.append({"build": build, "trace": "loader_demo", **r})
        log_text += f"=== {build} ===\n{out}"
        checks[f"[{build}] early_drop == expected DROPs in labels"] = int(r["early_drop"]) == want_drops
        checks[f"[{build}] one rulebook refused, one installed"] = (r["ctrl_ok"], r["ctrl_refused"]) == ("1", "1")
        checks[f"[{build}] both control replies sent"] = r["tx_ctrl"] == "2"
        checks[f"[{build}] every echo answered"] = r["echo_rx"] == r["tx_echo"] == want_echo
    with open(os.path.join(out_dir, "demo.csv"), "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(demo_rows[0].keys()))
        w.writeheader()
        w.writerows(demo_rows)
    with open(os.path.join(out_dir, "demo_log.txt"), "w") as f:
        f.write(log_text)
        for k, v in checks.items():
            f.write(f"CHECK {'PASS' if v else 'FAIL'}: {k}\n")
    if not all(checks.values()):
        sys.exit(f"live-update demo check failed: {checks}")
    print("demo: all checks pass")

    # 4. footprint
    with open(os.path.join(out_dir, "footprint.txt"), "w") as f:
        for build, elf in ELFS.items():
            f.write(f"=== {build}: {os.path.relpath(elf, ROOT)} ===\n" + sh([f"{ARM_BIN}/arm-none-eabi-size", "-A", elf]))
    with open(os.path.join(out_dir, "xc_check.txt"), "w") as f:     # per-object code size and stack
        f.write(sh(["sh", os.path.join(ROOT, "tools", "xc_check.sh")]))

    # 5. meta
    with open(os.path.join(out_dir, "meta.txt"), "w") as f:
        f.write(f"date: {datetime.datetime.now().astimezone():%Y-%m-%d %H:%M:%S %Z}\n")
        f.write(f"commit: {sh(['git', 'rev-parse', 'HEAD'], cwd=ROOT).strip()}{' (dirty)' if dirty else ''}\n")
        f.write(sh(["qemu-system-arm", "--version"]).splitlines()[0] + "\n")
        f.write(sh([f"{ARM_BIN}/arm-none-eabi-gcc", "--version"]).splitlines()[0] + "\n")
        f.write("machine: mps2-an505 (Cortex-M33); simulated ST driver + lwIP 2.2.1; -O2\n")
        f.write("builds: nosys (lwIP NO_SYS) and rtos (FreeRTOS 11.2.0 ARM_CM33_NTZ, QEMU -icount shift=0)\n")
        f.write(f"matrix: traces={FUNC_TRACES} configs={CONFIGS} loops={LOOPS} bursts={BURSTS}\n")
        for t in FUNC_TRACES + ["measure", "loader_demo"]:
            with open(os.path.join(TRACES, t + ".pcap"), "rb") as p:
                f.write(f"sha256 {t}.pcap {hashlib.sha256(p.read()).hexdigest()}\n")
        f.write("command: python3 bench/run_sim.py\n")
    print(f"saved to {os.path.relpath(out_dir, ROOT)}")


if __name__ == "__main__":
    main()
