# CLAUDE.md — Project memory for Claude Code

> Put this file at the **root of the project repository**. Claude Code reads it at the
> start of every session. Keep it short and true; detail lives in `docs/`.

## 1. What this project is

**MTech thesis (MTP, 2 students):** *Lightweight Programmable Packet Processing Framework &
Execution Layer for STM32-Based RTOS.* Supervisor: Prof. Kaushik Saha.

An **XDP/eBPF-inspired** packet-processing framework for a **bare-metal FreeRTOS + lwIP** MCU.
A small, **verified, heap-free bytecode VM** runs packet-filtering programs (e.g. a DDoS
filter) at the **Ethernet RX driver layer, before lwIP**, returning PASS/DROP. A second hook
at lwIP's `ip4_input()` gives a late-path baseline. Results are compared against
native-C filtering (and the MAC's hardware L3/L4 filter) on latency, CPU load, throughput,
memory footprint and RTOS timing overhead.

## 2. Team and thesis attribution

One developer works on both roles (Kiran works on the same machine). The names below are for
**attribution in the thesis and presentations only. They are not editing restrictions:** any
module may be edited.

| Area | Attributed to | Paths |
|---|---|---|
| Role 1: VM & Dynamic Execution Layer (ISA, verifier, interpreter, assembler, bytecode programs) | **Kiran (2025EET2800)** | `firmware/VM_Engine/`, `tools/asm/`, `tests/host/`, `firmware/DDoS_Prototype/ddos_bytecode.h` |
| Role 2: XDP-Inspired Packet Processing & RTOS Integration (driver hook, lwIP hook, FreeRTOS/lwIP bring-up, measurement, traffic bench) | **Nishant (2025EET2734)** | `firmware/Network_Hooks/`, `stm32_project/`, `bench/` |
| Joint: DDoS prototype demo, evaluation, thesis | both | `firmware/DDoS_Prototype/`, `eval/`, `thesis/` |

Attribution is recorded in git: every commit message starts with `role1:`, `role2:` or `joint:`.
All work is committed directly on `main` (no per-person branches).

## 3. Platform (verified facts)

- Board **NUCLEO-H563ZI**, MCU **STM32H563ZIT6**: Arm Cortex-M33 (TrustZone, FPU), up to **250 MHz**,
  **2 MB flash, 640 KB SRAM**, 10/100 Ethernet MAC with DMA, **RMII + LAN8742 PHY**, on-board **ST-LINK/V3EC**.
  User LEDs: LD1 PB0, LD2 PF4, LD3 PG4. User button PC13.
- References: RM0481 (H563/H573 reference manual), UM3115 (Nucleo user manual), STM32H5 HAL ETH driver.
- Dev machine: one shared **MacBook Air M1** (macOS, Apple silicon; no Ethernet port, so a USB-C
  Ethernet adapter is needed; Rosetta 2 is installed; see `docs/MAC_SETUP.md`).
  On macOS the serial console is `/dev/cu.usbmodem*`; read it with pyserial and a timeout.
  Line-rate flood tests need a Linux PC.
- **STM32CubeMX does NOT generate lwIP for STM32H5.** lwIP comes from ST's
  `stm32h5-classic-coremw-apps`. Its `ethernetif.c` owns the ETH RX callbacks and a zero-copy
  `RX_POOL` of custom pbufs. Our base is its NUCLEO-H563ZI example `LwIP_UDPTCP_Echo_Server_Netconn_RTOS`,
  package tag **v2.1.0** (its README: HAL 1.6.0, FreeRTOS 11.2.0, lwIP 2.2.1, tested with
  STM32CubeIDE 2.1.0). The example ships a CubeIDE project and **no `.ioc`**. Since CubeIDE 2.0,
  CubeMX is a separate tool. Exact pinned versions: `docs/PLATFORM.md` (once created).
- H5 HAL ETH RX API (checked against `stm32h5xx_hal_eth.h` in HAL v1.7.0, and again on 28 Oct 2026 in
  the pinned HAL v1.6.0):
  - `void HAL_ETH_RxAllocateCallback(uint8_t **buff);` — failure = leave `*buff == NULL` (returns **void**).
  - `void HAL_ETH_RxLinkCallback(void **pStart, void **pEnd, uint8_t *buff, uint16_t Length);` — called
    per completed descriptor from `HAL_ETH_ReadData()`, **in the caller's (RX task) context**.
  - `void HAL_ETH_RxCpltCallback(ETH_HandleTypeDef *heth);` — **ISR context**.
  - If the link callback links nothing (our DROP), `HAL_ETH_ReadData()` returns **HAL_OK with a NULL
    buffer**. The refill path calls `RxAllocateCallback` only for descriptors whose `BackupAddr0 == 0`.
  - `HAL_ETH_ReadData()` handles **one frame per call**. A descriptor is processed only if it is a
    frame's first segment or a frame is already being linked. So if the hook drops segment 1, the frame's
    later segments are skipped and keep their buffers.
- ST example facts (v2.1.0, read from the files): `RX_POOL` = 12 × `RxBuff_t` of 1000 B
  (`ETH_RX_BUFFER_SIZE`), 4 RX descriptors; RX task `ethernetif_input` at `osPriorityRealtime`, 1024 B
  stack; `tcpip_thread` at `osPriorityHigh`, 2048 B (CMSIS-RTOS2 stack sizes are **bytes**); semaphore
  `RxPktSemaphore`; 1 kHz tick; `configMAX_SYSCALL_INTERRUPT_PRIORITY` = 5 << 4; echo servers on UDP/TCP
  port 7; log on USART3 at 115200 baud. Licences: example app files SLA0044 (keep ST notices, ST MCUs only),
  HAL/BSP/lwIP BSD-3, FreeRTOS MIT, CMSIS Apache-2.0.
- The H5 ETH MAC has a **hardware L3/L4 filter** (2 slots: `HAL_ETHEx_SetL4FilterConfig`, etc.) and a
  missed-frame counter (`ETH->DMACMFCR`). Use both in the evaluation.
- lwIP hook registration: `#define LWIP_HOOK_FILENAME "lwip_ip4_hook.h"` +
  `#define LWIP_HOOK_IP4_INPUT(p, inp) lwip_ip4_hook_input(p, inp)` in `lwipopts.h`.
  **Never `#include` lwIP headers from `lwipopts.h`** (causes circular-include errors).
  Hook returns non-zero ⇒ packet consumed ⇒ the hook must `pbuf_free()` it.

## 4. Repository layout

```
CLAUDE.md, README.md       this file; bundle overview and changelog
docs/                      PLAN_M1_M2 (current plan, tick boxes as we go), ISA (VM ISA v1 spec),
                           ROADMAP, KNOWN_ISSUES, EVALUATION_PLAN, MAC_SETUP, LOG (session log), KICKOFF_PROMPT,
                           INTEGRATION_NOTES_v0 (superseded where KNOWN_ISSUES says so),
                           Project_Dossier.{pdf,tex}, reference/ (mid-term deck, guide, role PNGs)
firmware/VM_Engine/        vm_isa.h, vm_verifier.[ch], vm_interpreter.[ch]      (role 1)
firmware/Network_Hooks/    xdp_dma_hook.[ch], lwip_ip4_hook.[ch]                (role 2)
firmware/DDoS_Prototype/   ddos_bytecode.h, ddos_prototype_init.c               (joint)
tests/host/                host unit tests: Makefile, test_vm.c (no hardware)   (role 1)

Not created yet (see docs/PLAN_M1_M2.md):
stm32_project/             CubeIDE project from ST's example. Our app/port files in Core/, LWIP/;
                           vendor code (never edited) in Drivers/, Middlewares/; VENDOR.md
docs/PLATFORM.md           pinned tool and library versions
tools/asm/                 Python assembler/disassembler                         (role 1)
bench/                     PC-side scripts: UART log, echo test, floods          (role 2)
eval/                      raw results (CSV/logs), plotting scripts, figures     (joint)
thesis/                    thesis sources                                        (joint)
```
The ST package clone used as the vendor source lives **outside** the repo:
`~/Projects/vendor/stm32h5-classic-coremw-apps` (tag v2.1.0, read-only reference).

## 5. Commands

- Host unit tests (run after **every** VM change): `make -C tests/host` and `make -C tests/host asan`
- Cross-compile check for the Cortex-M33 (VM engine, hooks, and lwIP `ip4.c` with our hook, against the
  pinned ST package; prints size and stack; objects in git-ignored `build/xc/`): `sh tools/xc_check.sh`
- Which compiler is used for what: host tests build with the Mac's `cc` (Apple clang); the
  cross-compile check uses the Arm GNU Toolchain 15.2.1 (`/Applications/ArmGNUToolchain/`);
  firmware is built by CubeIDE's bundled GCC. **Footprint numbers for the thesis must come from the
  compiler that builds the firmware**, with its version recorded.
- Footprint: `arm-none-eabi-size -A build/*.elf` and the `.map` file; stack: `*.su` from `-fstack-usage`.
- Target build/flash: STM32CubeIDE (headless build if available) and STM32CubeProgrammer CLI
  (`STM32_Programmer_CLI -c port=SWD -w build/app.elf -v -rst`). If the board or tools are not
  reachable from this machine, **ask the user to run it and paste the output**. Never pretend to
  have observed hardware behaviour.

## 6. Engineering rules (non-negotiable)

1. **No heap** in VM_Engine or Network_Hooks (`malloc`, `pvPortMalloc`, VLAs: forbidden). Static or stack only.
2. **Every bytecode program passes `vm_verify()` before it can be installed.** Install APIs must enforce this.
3. **Fail safe:** any VM fault (OOB load, bad input, budget) ⇒ DROP, never PASS.
4. **ISR discipline:** ISRs only signal (FromISR APIs). The VM runs in the RX task unless a build flag
   explicitly selects the "VM-in-ISR" experiment.
5. **Do not edit vendor code**: HAL, CMSIS, BSP, lwIP core, FreeRTOS kernel (everything under
   `stm32_project/Drivers/` and `stm32_project/Middlewares/`). ST's **application/port files** copied
   into our project (`ethernetif.c`, `lwipopts.h`, `FreeRTOSConfig.h`, `main.c`, `app_ethernet.c`, ...)
   are ours to edit: keep changes small and mark each with an `/* XDP: ... */` comment. Prefer
   callbacks and hooks; where a file has `/* USER CODE BEGIN/END */` blocks, put changes inside them.
6. Compile with `-Wall -Wextra -Werror` for project code. Fix warnings, don't silence them.
7. Small steps: change → compile → run host tests → commit. One logical change per commit, and
   the message starts with `role1:`, `role2:` or `joint:`.

## 7. Research-integrity rules (this is a thesis)

- **Never invent numbers.** Every number in docs/slides/thesis comes from a logged run in `eval/`
  (raw CSV + script + config + git commit hash + date).
- **Claims must match code.** If a slide/thesis says it, the code must do it (see
  `docs/KNOWN_ISSUES.md` → "Deck vs implementation gaps").
- Cite only papers actually read; verify every citation (authors, venue, year).
- Report negative results honestly (e.g. interpretation overhead vs native C).

## 8. Current status (update at the end of each session)

Last updated 24 Nov 2026, evening (final sprint: Day 1 and Day 2 done). Details: `docs/LOG.md`.
Plan: `docs/PLAN_FINAL_SPRINT.md`. Plain-language guide: `docs/WALKTHROUGH_SIMPLE.md`.

- **No physical board.** Everything runs on QEMU mps2-an505 (Cortex-M33). Submission and presentation:
  27 Nov. Never present a simulated number as a hardware measurement.
- **Repo:** `main`, pushed to the private GitHub repo `nishantsharma198/MTP_XDP_STM32` at checkpoints.
  Workflow-only docs are removed from the published version before the final push.
- **VM:** ISA v1 frozen. D9/D11 fixed, `memset` removed (−39 %). `vm_loader`: XVM1 messages (CRC,
  verify, per-hook double buffer); `tools/asm/bcgen.py` assembler. Gap G9 addressed in simulation
  (unauthenticated).
- **Hooks:** pure functions (`hook_common.h`); native C filter matches the VM on 75,065 frames;
  `EXP_CONFIG` C0–C4.
- **Simulation:** `sim/eth_sim` (ST driver/HAL RX model incl. both D5 loops, real lwIP 2.2.1, UDP echo,
  control port 9000), `sim/rtos` (FreeRTOS 11.2.0, ST task layout, real NVIC interrupt, `-icount`).
  Runner `python3 bench/run_sim.py` → `eval/raw/<date>_<commit>_sim/`; then `python3 eval/flood_model.py <run>`
  and `python3 eval/plot.py <run>` → `eval/figures/`.
- **Tests:** 209/209 on the Mac (also ASan/UBSan) and on the emulated M33.
- **Key results** (`eval/raw/2026-11-24_3828f4c_sim`, FreeRTOS build, instructions per attack frame):
  C0 5014, C1 2952, C2 3094, C3 1531, C4 1726. Legit echo C4 5104 vs C0 4888. D5 demonstrated (C4 with ST's
  loop answers 55 % / 29 % of echoes at 2 / 4 frames per wake-up; fixed loop 100 %). Flood MODEL: C4 keeps
  100 % legit at 100 Mbit/s only at CPI 1; at CPI 2 max ~69k frames/s. VM engine code 2,899 B.
- **Next (Day 3):** docs to the no-board reality (EVALUATION_PLAN, README), report and slides once the format
  is known, rehearsal, interview file numbers, final push (clean published version).
