# Plan: final MTP sprint, 24–27 Nov 2026 (everything simulated)

Approved 24 Nov 2026. This replaces `docs/PLAN_M1_M2.md`, whose hardware steps no longer apply.
Learning material for each step: `docs/WALKTHROUGH.md`.

## Context
- There is no physical board. **Final submission and presentation are both on 27 Nov.** Joint working
  sessions start today around 11:30 or after lunch.
- Nishant and Kiran must both understand every piece of code and every result, well enough to
  present and defend it.
- The resume bullets are frozen. The evaluation must produce honest evidence for them:
  - the ingress hook working (in emulation);
  - the lwIP-hook baseline;
  - footprint, CPU cost and flood behaviour compared with native C;
  - the DDoS prototype.
- The story for the evaluation change: "planned on the board; hardware unavailable; moved to
  emulation and simulation".

**Approach:**
- Run our real C code on an **emulated Cortex-M33** (QEMU `mps2-an505`, already installed: same CPU as
  the STM32H563).
- Use the **real lwIP 2.2.1** from ST's package, and a **simulated ST Ethernet driver** fed from
  packet traces.
- Measure **instructions executed per packet** (QEMU's one-instruction-per-block trace) and **code
  size** (from the real Arm compiler).
- Build a simple **flood model** on top of those measured costs.

| Simulation can show | Simulation can't show (stated as limitations) |
|---|---|
| Correct verdicts on real traffic patterns; legit traffic still delivered | Wall-clock latency, real CPU %, real interrupt timing |
| Instructions per packet: VM vs native C, early vs late hook | Exact cycles (instruction count = lower bound on cycles) |
| Code/RAM footprint for Cortex-M33 | MAC hardware filter (C5), VM-in-ISR (C6) |
| Flood behaviour from a model based on measured costs | Real collapse point on silicon |

**Legend:** [me] = Claude builds and runs it · [both] = Nishant and Kiran read and ask questions ·
[you] = your decision. Every build step ends with an explanation before moving on.

## Now, before the joint session (this morning)
0. **[me]** Write `docs/WALKTHROUGH.md` part 1 (the VM, in plain words, file by file) so Nishant can
   study it and brief Kiran. No code changes until you both sit together. **[you]** Read it and note
   questions.

## Day 1 · Tue 24 Nov (from ~11:30): understand the VM, finish the software, bring up the emulator
1. **[both] Walkthrough 1, the VM**, using `docs/WALKTHROUGH.md` part 1, covering:
   - `vm_isa.h`, `vm_verifier.c`, `vm_interpreter.c`
   - `ddos_bytecode.h` and `tests/host/test_vm.c`

   You run `make -C tests/host` yourselves.
2. **[me] `role1:` D11 and D9.** `insns_executed` filled on every exit path. A `vm_slot` install helper
   (verify first, refuse a `ctx` mismatch, volatile pointer). Tests.
3. **[me] `role2:` hook rewrite (D2, D6, D8).** `xdp_dma_hook.c` becomes a pure function,
   `xdp_hook_run(frame, len)`, with counters and no HAL dependency. Both hooks install through
   `vm_slot`.
4. **[me] `role2:` native-C DDoS filter** (Ethernet and IP variants), with a host differential test:
   VM vs native on thousands of random and truncated frames must give identical verdicts.
5. **[me] `joint:` `EXP_CONFIG` C0–C4** build switch (no filter / native or VM × lwIP or driver hook).
6. **[me] `role2:` emulator harness** `sim/qemu_m33/`:
   - startup code and linker script for the AN505 board, semihosting output, a Makefile
   - run the whole host test suite on the emulated Cortex-M33
   - a script that counts instructions between two marker functions
7. **[both] Walkthrough 2:** hooks, native filter, emulator harness.

## Day 2 · Wed 25 Nov: datapath simulation, measurements, plots
8. **[me] `role2:` simulated ST driver** `sim/eth_sim.c`, copying the behaviour of ST's
   `ethernetif.c` and HAL that we read in October:
   - zero-copy `RX_POOL` of 12 × 1000-byte buffers and a 4-descriptor ring
   - `ReadData` handles one frame per call and returns "OK, but no frame" after a drop
   - our hook in `RxLinkCallback`
   - both receive loops: ST's original and the fixed one (D5)
9. **[me] `role2:` real lwIP 2.2.1** (no-RTOS mode, compiled into the emulated firmware) with
   `LWIP_HOOK_IP4_INPUT` and a UDP echo service on port 7.
9b. **[me] `role1:` + `role2:` bytecode loader (closes gap G9, added 24 Nov).** New rules reach the
    chip as data, with no rebuild:
    - **Format:** a small header (magic `XVM1`, ISA version, ctx, instruction count, CRC-32) followed
      by the 8-byte instructions.
    - **`vm_loader.c` (role1):** checks the header and CRC, then copies the program into one of **two
      static RAM buffers** (no heap; the buffer in use is never overwritten). It installs only
      through `vm_slot_install()`, so every loaded program is verified and ctx-checked.
    - **Channel (role2):** a UDP "control" port in the simulated firmware (lwIP, from step 9).
      A control packet carries the program; the reply says OK or which rule refused it.
    - **`tools/bcgen.py`:** writes a program from a short text form into the binary format (a minimal
      start on the assembler, G7).
    - **Tests:** a bad CRC, a wrong ctx, a wrong version, a truncated program and an unverifiable
      program are each refused, and the old program keeps running.
    - **Demo trace:** mid-trace, a control packet changes the target port from 1900 to 53. Packets
      before it follow the old rule and packets after it follow the new one, with no rebuild.
    - **Stated limitation:** the verifier makes a loaded program **safe**, but it doesn't say **who**
      may load it. Authentication of the sender (e.g. a signature or an allowed management address)
      is future work.
9c. **[me] `role2:` FreeRTOS on the emulated Cortex-M33 (moved from stretch, 24 Nov).** Answers
    "was it tested under the RTOS?" honestly: yes, on an emulated M33 (not on the STM32 chip).
    - **What:** FreeRTOS 11.2.0 from ST's package (port `GCC/ARM_CM33_NTZ`, vendor code not edited),
      SysTick at 1 kHz, our own `FreeRTOSConfig.h` with `configENABLE_FPU 1` (the D13 fix). lwIP 2.2.1
      switches from no-RTOS mode to RTOS mode (lwIP's FreeRTOS port), with `tcpip_thread` and its mailbox.
    - **Tasks, mirroring ST's example:** the RX task (highest priority) runs the simulated driver and
      the early hook. A simulated "interrupt" gives the RX semaphore, as ST's ISR does. `tcpip_thread`
      (one level lower) runs lwIP and the late hook.
    - **Measured** (instruction counts, as on Day 1): per packet, for C0–C4, from the RX task waking up
      until the packet is dropped or delivered. This includes the mailbox hand-off and the task switch
      that early filtering skips, i.e. the real **placement** result (C1 vs C3, C2 vs C4).
    - **Checks:** no stack overflow (FreeRTOS check level 2), no `configASSERT`, every packet accounted
      for. The UDP echo still answers during a flood trace in C3 and C4.
    - **Fallback:** if FreeRTOS isn't running in QEMU by about mid-afternoon on Day 2, keep the no-RTOS
      lwIP results, and estimate the hand-off cost from FreeRTOS's code path, labelled as an estimate.
10. **[me] `joint:` traffic traces** `bench/gen_traces.py` (plain Python, no scapy):
    - legit: ping, UDP echo, DNS, ARP
    - attack: UDP flood to port 1900
    - mixes, plus odd and truncated frames
11. **[me] `joint:` experiment runner** `bench/run_sim.sh`: configs C0–C4 × traces → CSV files in
    `eval/raw/<date>_<commit>/`. It records:
    - verdict counts and echoes delivered
    - instructions per packet on each path
    - footprint (`arm-none-eabi-size`, map file)
12. **[me] `joint:` flood model** `eval/flood_model.py`:
    - measured instructions per packet → lower-bound cycles at 250 MHz
    - the highest packet rate each config can keep up with
    - how much legit traffic survives as the flood rate rises

    Clearly labelled as a model. It also includes a **D5 demo** comparing stranded frames with ST's
    loop vs the fixed loop.
13. **[me] `joint:` plots** `eval/plot.py` → `eval/figures/`, all generated from the CSV files.
14. **[both] Walkthrough 3:** simulation and results. **[you]** Pick what goes on the slides.
15. **Stretch, only if Day 2 is on time:**
    - RQ3 static max-offset analysis and a fast path without bounds checks
    - a computed-goto interpreter

## Day 3 · Thu 26 Nov: docs, report, slides, rehearsal
16. **[me] `joint:` docs updated to the no-board reality:** `EVALUATION_PLAN.md`, `CLAUDE.md`,
    `KNOWN_ISSUES.md`, `PLAN`, `LOG`.
17. **[me] Report and presentation** in the required format once the instructions arrive. Default:
    a Word report and a PowerPoint deck (both installed). Figures come from `eval/`, and every number
    traces back to a CSV and a commit. Includes limitations and the "plan changed" story, consistent
    with the resume bullets.
18. **[both] Q&A rehearsal.** I update the NIC interview file with the real simulation numbers.
19. **[you] Push to a private GitHub repo.** I'll ask for your OK at that point.

**Fri 27 Nov:** final check of the deliverables in the morning (re-run tests and the experiment runner,
check every number against `eval/`), **submission**, then the **presentation**.

## Ground rules (unchanged)
- Small commits, prefixed `role1:` / `role2:` / `joint:`.
- Host tests and ASan after every change.
- No heap in the VM or hooks.
- Never invent numbers: every figure comes from a logged run.
- Never present simulation as hardware.

## Verification
- `make -C tests/host` and `make -C tests/host asan` stay green; the test count only grows.
- Differential test: native vs VM verdicts identical on random and truncated frames.
- The same test suite passes on the emulated Cortex-M33 (QEMU).
- The experiment runner is deterministic: a re-run gives identical CSV files.
- Every figure is regenerated by script from CSV, tagged with the commit hash.

## Risks and fallbacks
- **QEMU instruction tracing is slow or gives huge logs:** count on short traces; for straight-line
  paths, also count instructions from the disassembly.
- **lwIP won't integrate on the bare-metal emulator in time:** run lwIP as a Mac host build for
  correctness, and use QEMU only for the instruction counts of the VM, native filter and driver path.
- **Time runs short:** drop the stretch items first; start the report and slides on Day 2 evening.
- **The deliverable format is unknown:** draft early, adapt when the instructions arrive.
- **Folder move:** not needed (no CubeIDE). Keep the folder where it is to avoid disruption.
