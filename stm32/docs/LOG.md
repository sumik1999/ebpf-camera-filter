# Session log

Append one entry per working session: date, who, what changed, **what was verified and how**,
and open questions. Newest at the bottom.

---

## 2026-10-28: handoff audit (prepared with Claude, before moving to Claude Code)
**Changed**
- `firmware/VM_Engine/vm_isa.h`: added `#include <stddef.h>` (D1). The VM engine did not compile before this.
- Added `tests/host/` (33 unit tests) and its Makefile.
- Added CLAUDE.md and docs/ (KNOWN_ISSUES, ROADMAP, EVALUATION_PLAN, KICKOFF_PROMPT, dossier).

**Verified (how)**
- Host: `gcc -std=gnu11 -Wall -Wextra -Werror`: 33/33 tests pass, also under ASan + UBSan.
- Cortex-M33 (arm-none-eabi-gcc 13.2.1, `-mcpu=cortex-m33 -mfloat-abi=hard`), with `-Werror` clean:
  - `vm_interpreter.o` .text 1084 B (-O2) / 932 B (-Os); stack frame 80 B / 72 B; no .data/.bss.
  - `vm_verifier.o` .text 463 B / 454 B; stack 16 B / 20 B.
  - DDoS bytecode (both variants) ~144 B .rodata.
  - The interpreter `switch` compiles to one `TBH` jump table.
  - DDoS DROP path = 7 VM instructions; non-IPv4 PASS path = 3.
- Network hooks compiled against STM32H5 HAL v1.7.0 + lwIP 2.2.x + FreeRTOS-Kernel headers:
  - `xdp_dma_hook.c` fails with *conflicting types for HAL_ETH_RxAllocateCallback* (D2).
  - `lwip_ip4_hook.c` compiles. lwIP `ip4.c` compiles with the hook via `LWIP_HOOK_FILENAME` (D7 fix).
- Read the H5 HAL source: `HAL_ETH_ReadData()` returns HAL_OK + NULL buffer when the link callback
  links nothing (basis of D5). Refill allocates only for `BackupAddr0 == 0` descriptors.
- H5 MAC has hardware L3/L4 filters (`HAL_ETHEx_SetL4FilterConfig`) and `ETH->DMACMFCR`.

**Not verified:** nothing has been run on the NUCLEO-H563ZI by me. Hardware status is unknown here.

**Open questions for the team**
- Was the FreeRTOS + lwIP baseline (deck Phase 1, "completed") built from stm32h5-classic-coremw-apps? Which HAL version?
- Exact MTP-1 end-evaluation date (roadmap assumes early December 2026).

---

## 2026-10-28: session 1 in Claude Code (repo, discovery, ISA v1 draft, tests)
**Who:** Nishant (working on both roles), with Claude Code. Plan approved: `docs/PLAN_M1_M2.md`.

**Changed** (commits on `main`, oldest first)
- `joint: import v0.1.2 handoff bundle`: git repo created, `.gitignore` added.
- `joint: CLAUDE.md for single-developer workflow and real layout`: attribution-only roles, commit
  prefixes, real layout, compiler roles, rule 5 (ST app/port files are editable with marked changes);
  ROADMAP M1 and README updated.
- `joint: add M1/M2 plan`.
- `role1: ISA v1 specification (draft)`: `docs/ISA.md`.
- `role1: host tests for every ISA v1 opcode and verifier rule`: 33 → 110 checks.
- `role1: make sanitizer findings fail the asan test run`: `-fno-sanitize-recover=all`.
- This entry, plus CLAUDE.md status and KNOWN_ISSUES (D2/D5/D6 rechecked, D9 → Major, D12 documented,
  new D13, D14).

**Verified (how)**
- Host tests, Apple clang 21.0.0 (arm64, macOS 26.6.2): `make -C tests/host` gives 110 passed, 0 failed.
  `make -C tests/host asan` gives 110 passed, 0 failed.
- Do the tests catch bugs? 4 hand-made interpreter mutants (built in a scratch folder, never committed):
  - signed `JGT_K`: 1 failure
  - bounds check missing `- size`: 3 failures
  - corrupted `LDPKT_H`: 6 failures
  - shift mask 31 → 63: 0 failures in the normal build. It is undefined behaviour, and Apple silicon masks
    the shift in hardware. UBSan reports it; with the new flag the asan run exits 134.
- Cross-compile, Arm GNU Toolchain 15.2.Rel1 (arm-none-eabi-gcc 15.2.1), CLAUDE.md flags, `-O2 -Werror`:
  clean. Results:
  - `vm_interpreter.o`: .text 1052 B, stack 80 B
  - `vm_verifier.o`: .text 472 B, `vm_verify` stack 8 B
  - no .data/.bss
  - one `TBH` jump table in `vm_interpreter_run`

  Earlier entry, with gcc 13.2.1: 1084 B and 463 B; stack 80 B and 16 B. These are **not** thesis
  numbers: the firmware compiler (CubeIDE's GCC) isn't installed yet.
- Instruction byte layout: `{JNE_K, dst=3, src=5, off=-2, imm=0x12345678}` is `13 53 FE FF 78 56 34 12`
  on host clang and in the arm-none-eabi-gcc `.rodata`.
- **ST package.** Shallow clone of `github.com/STMicroelectronics/stm32h5-classic-coremw-apps`, tag
  v2.1.0 (commit `ba31bb74566e`, 9 Mar 2026), at `~/Projects/vendor/`. Only 7 submodules: HAL
  `2e49bfee9c1c`, CMSIS device `4a9d636e9854`, Nucleo BSP `2a659bfdb04c`, BSP Common `6893c33e9a5e`
  (v7.3.0), lan8742 `9f365d062b3c`, LwIP `07680c8172fa` (v2.2.1_20250804), FreeRTOS `bca405993934`.
  119 MB. Versions read from source: HAL 1.6.0 (`stm32h5xx_hal.c`), FreeRTOS V11.2.0 (`task.h`), lwIP 2.2.1
  (`init.h`), CMSIS Core 5.x. Licences (`LICENSE.md`): example apps SLA0044; HAL, BSP and lwIP BSD-3;
  FreeRTOS MIT; CMSIS Apache-2.0.
- **Example `LwIP_UDPTCP_Echo_Server_Netconn_RTOS`, read from the files.**
  - `ethernetif.c` defines `HAL_ETH_RxAllocateCallback`, `HAL_ETH_RxLinkCallback`, `HAL_ETH_RxCpltCallback`,
    `HAL_ETH_TxCpltCallback`, `HAL_ETH_TxFreeCallback` and `HAL_ETH_ErrorCallback`.
  - RX pool: `RX_POOL` = `ETH_RX_BUFFER_CNT` (12) × `RxBuff_t { struct pbuf_custom; uint8_t buff[1024] (32-byte aligned) }`.
    `ETH_RX_BUFFER_SIZE` is 1000 and `ETH_RX_DESC_CNT` is 4 (`stm32h5xx_hal_conf.h`). The free function
    `pbuf_free_custom` returns the buffer to the pool and, if allocation had failed, sets `RxAllocStatus`
    back to OK and wakes the RX task.
  - RX semaphore `RxPktSemaphore` (created with `xSemaphoreCreateBinary`). It is released in
    `HAL_ETH_RxCpltCallback` (ISR), in `HAL_ETH_ErrorCallback` on RBU, and in `pbuf_free_custom`.
  - RX task `ethernetif_input`: `osThreadNew`, `osPriorityRealtime`, stack 1024 **bytes**.
    `tcpip_thread`: `osPriorityHigh`, 2048 bytes, mailbox 6. Units: `cmsis_os2.c` divides `stack_size` by
    `sizeof(StackType_t)`.
  - **RX drain loop** (`LWIP/Target/ethernetif.c` lines 355–372 in the example; it will be in our repo
    after plan step 1.3). Paraphrase:
    ```
    for (;;)
      wait for RxPktSemaphore (forever)
      do:
        p = low_level_input(netif)     # calls HAL_ETH_ReadData() only while RxAllocStatus == RX_ALLOC_OK
        if p != NULL and netif->input(p, netif) != ERR_OK: pbuf_free(p)
      repeat while p != NULL           # source line: }while(p!=NULL);
    ```
  - `FreeRTOSConfig.h`:
    - tick 1000 Hz
    - `configMAX_PRIORITIES` 56
    - `configMAX_SYSCALL_INTERRUPT_PRIORITY` = 5 << 4 = 80
    - `configKERNEL_INTERRUPT_PRIORITY` = 15 << 4
    - heap_4, 20 KB
    - run-time stats **off**
    - stack-overflow check **off**
    - idle hook off
    - `configENABLE_FPU` 0 while `.cproject` uses `floatabi.value.hard` (new D13)
  - `lwipopts.h`: `LWIP_DHCP` 1, `MEM_SIZE` 14 KB at `0x20084000`, `PBUF_POOL_BUFSIZE` 1536,
    `CHECKSUM_BY_HARDWARE`, `LWIP_TCPIP_CORE_LOCKING` 1, no hooks, no `#include` lines.
  - `main.h`: IP 192.168.0.10/24, gateway 192.168.0.1 (used after 4 failed DHCP tries).
    `udpecho.c` / `tcpecho.c` bind port 7. `hal_conf`: `USE_HAL_ETH_REGISTER_CALLBACKS` 0, MAC
    00:80:E1:00:05:77.
  - `.cproject`: CubeIDE's own toolchain (`${gnu_tools_for_stm32_compiler_path}`), target STM32H563ZITx,
    non-secure. Debug `-O0`, Release `-Os`. Include paths reach the package root with 7 × `../`.
- **HAL 1.6.0 `HAL_ETH_ReadData()` / `ETH_UpdateDescriptor()`, read from source.**
  - A descriptor is processed only if it carries FD (first segment) or `pRxStart != NULL`.
  - It returns after one complete frame, with `*pAppBuff = pRxStart`. That is NULL when our hook linked
    nothing, which is the basis of D5.
  - If the hook drops segment 1 of a multi-segment frame, the later segments are skipped. Their
    `BackupAddr0` stays set, and the refill path re-arms the same buffer without allocating (D14).
- **Hooks compiled against the pinned headers** (example config, arm-gcc 15.2.1, `-Wall -Wextra`):
  - `xdp_dma_hook.c` fails: *conflicting types for 'HAL_ETH_RxAllocateCallback'*. D2 holds for HAL 1.6.0.
  - `lwip_ip4_hook.c` compiles with no warnings.

**Not verified:** nothing has been run on the NUCLEO-H563ZI. No firmware has been built. STM32CubeIDE
isn't installed. Facts about ST's example come from reading its files, not from running it.

**Tools on the Mac (28 Oct)**
- Present: Xcode CLT (make 3.81, git 2.54, clang 21), Homebrew, Rosetta 2, Arm GNU Toolchain 15.2.1,
  Python 3.10 (conda base) with matplotlib/pandas, python.org Python 3.12, Homebrew Python 3.14.
- Missing (plan 0.4): scapy, pyserial, tcpreplay, iperf3, STM32CubeIDE (+ STM32_Programmer_CLI).
- No board on `/dev/cu.usbmodem*` during the session.

**Open questions**
- ISA v1 freeze: answer Q1–Q6 in `docs/ISA.md` §11.
- D9 raised to Major: the interpreter's memory safety depends on verification (no run-time register
  range check). Agree?
- Which STM32CubeIDE version does st.com offer? 2.1.0 preferred, to match ST's tested combination.
- Still open from before: the exact MTP-1 end-evaluation date.

**Next session** (after moving the folder to `~/Projects/MTP_XDP_STM32`): check `git status` and the host
tests there. Then plan 1.2 → 1.7 (needs CubeIDE, the board and the USB-C Ethernet adapter). Phase 3
(D11, D9) can go ahead without hardware.

### 2026-10-28 (same session, later): ISA v1 frozen
**Decision:** you accepted all six recommendations in `docs/ISA.md` §11 (Q1–Q6).

**Changed** (commits on `main`)
- `role1: verifier requires unused instruction fields to be zero (ISA v1 Q1)`: new
  `VM_VERIFY_ERR_UNUSED_FIELD`.
- `role1: verifier rejects RET values other than 0 and 1 (ISA v1 Q2)`: new `VM_VERIFY_ERR_BAD_RET`. The
  interpreter still returns DROP for other values if a program is unverified.
- `role1: programs declare their hook context (ISA v1 Q5)`: `vm_program_t.ctx` (`VM_CTX_ETH` = 1,
  `VM_CTX_IPV4` = 2; 0 is invalid) and new `VM_VERIFY_ERR_BAD_CTX`. The DDoS programs declare their contexts.
- `role1: freeze ISA v1 (Q3, Q4, Q6 decisions; VM_ISA_VERSION 1)`: `VM_CLASS_RESERVED` (7); no run-time
  register check (Q3); compares stay unsigned (Q6); rules renumbered V1–V10 in check order.
- New result codes were appended to the enum, so existing values are unchanged.

**Verified (how)**
- `make -C tests/host`: 125 passed, 0 failed. `make -C tests/host asan`: 125 passed, 0 failed.
- arm-none-eabi-gcc 15.2.1, CLAUDE.md flags, `-O2 -Werror`: clean.
  - `vm_interpreter.o`: .text 1052 B, stack 80 B (unchanged)
  - `vm_verifier.o`: .text 765 B (was 472), `vm_verify` stack 12 B (was 8)
- Against the pinned ST headers (example config, `-Wall -Wextra`):
  - `lwip_ip4_hook.c` and `ddos_prototype_init.c` compile with no warnings.
  - `xdp_dma_hook.c` still fails with D2 (expected; plan 4.1).

**Not verified:** nothing has been run on hardware.

**Follow-up:** the install API must reject a program whose `ctx` doesn't match the hook. This is part of
D9 (plan 3.2); until then, nothing enforces it.

### 2026-11-24: final sprint Day 1 (no board; emulation only)
**Changed** (commits on `main`, pushed to the private GitHub repo first thing today at `2aa6ed9`)
- `role1`: D11, `insns_executed` filled on every exit path (`vm_finish`).
- `role1`: D9, `vm_slot` install API (verify, ctx check, keeps the old program on refusal, volatile pointer).
- `role2`: both hooks rewritten as pure functions on `hook_common.h`; the lwIP hook is split into core
  and glue. Fixes D2–D4, D6–D8; D7 and D10 handled.
- `role2`: `tools/xc_check.sh` (Cortex-M33 compile check, including lwIP `ip4.c` with our hook).
- `role2`: native C DDoS filter plus differential test. `joint`: `EXP_CONFIG` C0–C4.
- `role2`: QEMU harness `sim/qemu_m33/` (test suite on the M33, instruction counting, `record` to `eval/raw/`).
- `role1`: the interpreter zeroes its registers without `memset` (found from the QEMU trace).
- `joint`: walkthrough part 2.

**Verified (how)**
- `make -C tests/host` and `asan`: 190 passed, 0 failed. `make -C sim/qemu_m33 test`: 190 passed (QEMU exit 0).
- New tests checked against deliberately broken code: 4 D11 tests fail on the old interpreter; 3
  planted native-filter bugs are caught; a missed r7 zero is caught.
- QEMU method calibration: 10 NOPs measured as 10. The re-run CSV is byte-identical.
- Results: `eval/raw/2026-11-24_96f85bd_qemu_insns-{O2,Os}` (before the memset fix) and
  `eval/raw/2026-11-24_c8c8139_qemu_insns-{O2,Os}` (after).

**Not verified:** nothing has run on hardware (there is none). QEMU gives instruction counts, not time.

**Open questions:** none blocking. The report/slides format is still to come from the department.

### 2026-11-24 (evening): final sprint Day 2 done early
**Changed** (commits on `main`, pushed at checkpoints)
- `role2`: `sim/eth_sim`, a simulated ST Ethernet driver:
  - MAC/RX-DMA ring and HAL 1.6.0 `ReadData`/refill logic;
  - ST `ethernetif.c` RX side with the early hook;
  - ST's and the D5-fixed RX loop;
  - real lwIP 2.2.1 with ST's options and the late hook.

  It builds for the Mac (also with the sanitizers) and for QEMU.
- `joint`: `bench/gen_traces.py` (pcap traces) and `bench/run_sim.py` (experiment runner).
- `role1`: `vm_loader` (XVM1) and `tools/asm/bcgen.py`. `role2`: UDP control port 9000. `joint`: the
  `loader_demo` trace with automatic checks. Gap G9 addressed in simulation.
- `role2`: `sim/rtos`, FreeRTOS 11.2.0 on the emulated M33:
  - ST's priorities and stacks, a real NVIC interrupt, the RX task, and tcpip_thread with its mailbox;
  - D13 fixed in our config;
  - deterministic with `-icount`.
- `joint`: `eval/flood_model.py` (labelled MODEL), `eval/plot.py` (6 figures), simple walkthrough part 3.
- `docs/KNOWN_ISSUES.md`: D5 fixed and demonstrated; D13 fixed in simulation; new D15 (RX_POOL exhaustion
  in C0 under bursts).

**Verified (how)**
- 209 host checks (Mac, ASan/UBSan, emulated M33).
- 80 and 100 cross-runs gave identical results Mac vs QEMU, and no-RTOS vs FreeRTOS at burst 1 (60 of 60 in
  the final run).
- Demo checks: 8/8 pass.
- The measurement calibration holds (10 NOPs = 10). Repeated runs give byte-identical `measure.csv`.

**Not verified:** anything on hardware. The flood numbers are a model (CPI assumption). The live update is
unauthenticated.
