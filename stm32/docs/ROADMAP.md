# Roadmap (from 28 Oct 2026)

**Calendar assumption:** two-semester MTP. Part 1 ends with an evaluation around the
**first week of December 2026**; Part 2 runs **January–May 2027** and ends with the thesis
and defence. Shift the dates to your department's calendar. The order of work is what matters.

Each milestone has a **Definition of Done (DoD)**. Don't mark it done until the DoD holds on hardware.

---

## Part 1: remaining ~5 weeks

### M1 · Week of 28 Oct: Repo and hardware baseline
- **Nishant:** FreeRTOS + lwIP running on NUCLEO-H563ZI from ST's `stm32h5-classic-coremw-apps`
  (CubeMX doesn't generate lwIP for H5). Static IP, ping, UDP echo. Pin and record the HAL /
  CubeIDE versions in `docs/`.
- **Kiran:** freeze **ISA v1** (write `docs/ISA.md`: encoding table, semantics, verifier rules).
  Extend `tests/host`.
- **Both:** one git repo containing CLAUDE.md and this bundle. Commit directly on `main`; every
  message starts with `role1:`, `role2:` or `joint:` (thesis attribution). Step-by-step plan for
  M1 and M2: `docs/PLAN_M1_M2.md`.
- **DoD:** board answers ping and UDP echo; `make -C tests/host` passes; versions recorded.

### M2 · Week of 4 Nov: Correct datapath integration
- **Nishant:** fix D2–D7 (`docs/KNOWN_ISSUES.md`). The DMA/driver hook goes inside ST's
  `HAL_ETH_RxLinkCallback`, DROP returns the buffer via `pbuf_free()` to `RX_POOL`, and the drain loop
  uses the `ReadData` status. lwIP hook via `LWIP_HOOK_FILENAME`. Write a **native-C filter** with
  identical logic for both hook points.
- **Kiran:** D9 (verify-on-install), D11. Start **static max-offset analysis** in the verifier
  (gap G3).
- **DoD:** with the DDoS filter on, a UDP flood to port 1900 is dropped while ping and UDP echo on
  other ports keep working. No HardFault over a 10-minute flood. Stats counters are consistent.

### M3 · Week of 11 Nov: Measurement and traffic bench
- **Nishant:** DWT `CYCCNT` cycle counting around the verdict, GPIO probes (ISR entry → verdict) for the
  logic analyser/scope, FreeRTOS run-time stats, idle-hook CPU load, `ETH->DMACMFCR` missed-frame
  counter, and a periodic UART stats dump. Compile-time switch `EXP_CONFIG` = C0…C6 (see the
  evaluation plan). PC bench in `bench/`: scapy/hping3 floods at set rates, iperf/ping for legit
  traffic, CSV logging.
- **Kiran:** **Python assembler + disassembler** (`tools/asm/`), producing a C array and a `.bin`.
  **DDoS bytecode v1**: IHL-aware, TCP+UDP, several ports or a range, skips non-first fragments.
- **DoD:** one command runs an experiment and writes a CSV tagged with the git commit hash.

### M4 · Week of 18 Nov: First results
- Run C0–C4 at 3–5 flood rates. First plots: cycles per verdict, CPU load vs flood rate, and
  legit goodput/RTT vs flood rate.
- **DoD:** plots in `eval/figures/`, produced from raw CSVs by a script.

### M5 · Week of 25 Nov: MTP-1 deliverables
- Fix deck gaps G1–G8. Write the MTP-1 report: problem, related work, design, preliminary results.
  Record a 2-minute demo video (flood on, legit traffic survives).
- **DoD:** report and deck reviewed by the supervisor. Every number is traceable to `eval/`.

---

## Part 2: January–May 2027

| Month | Kiran (Role 1) | Nishant (Role 2) | Joint |
|---|---|---|---|
| **Jan** | Computed-goto dispatcher vs switch (benchmark). Optional verified 512-B scratch stack. | **Runtime program update** over a UDP control port: CRC, `vm_verify()` before activation, double-buffered atomic swap. This makes the "dynamic execution layer" claim real. | Demo: change the blocked port live, with no reflash. |
| **Feb** | **Stateful extension:** bounded per-source counter map, enabling a **rate-limiting** DDoS filter that the hardware L3/L4 filter cannot do. Verifier fuzzing (libFuzzer/AFL on host). | Full evaluation matrix incl. C5 (hardware L3/L4 filter) and C6 (VM-in-ISR experiment). Line-rate tests (100 Mbit/s small frames). | Freeze the experiment protocol. |
| **Mar** | Thesis chapters: ISA, verifier (with proof sketch of termination and memory safety), interpreter. | Thesis chapters: datapath integration, RTOS timing, evaluation methodology. | Results analysis. Optional workshop-paper draft. |
| **Apr** | Final numbers and reproducibility package. | Final numbers and reproducibility package. | Full thesis draft to supervisor. |
| **May** | Defence prep. | Defence prep. | Submission and defence. |

## Stretch ideas (only after the core is solid)
- Bytecode signing (authenticated updates).
- Use TrustZone to isolate the VM (Cortex-M33 has it).
- A tiny C-subset → bytecode compiler (instead of hand assembly).
- An XDP_TX-style "reflect" action (answer simple probes directly from the driver).
