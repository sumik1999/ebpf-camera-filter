# Plan: milestones M1 + M2 (both roles)

> **Superseded on 24 Nov 2026** by `docs/PLAN_FINAL_SPRINT.md`. There is no physical board, so the
> hardware steps below no longer apply; the evaluation moved to emulation and simulation. Kept for
> history.

Approved 28 Oct 2026. Tick a box (`[x]`) in the same commit that finishes the step.
Milestone goals and Definitions of Done: `docs/ROADMAP.md`. Defect IDs (D1–D12, G1–G8): `docs/KNOWN_ISSUES.md`.

## Context
What discovery found on 28 Oct 2026:
- There is no STM32 project, vendor code (HAL, lwIP, FreeRTOS) or STM32CubeIDE on the Mac.
- The folder was not under git.
- scapy, pyserial, tcpreplay, iperf3 and CubeIDE were missing.
- The v0 driver hook does not compile against the H5 HAL (D2–D7).
- The VM engine is fine: 33/33 host tests pass, including ASan.

Goals:
- **M1:** a git repo, a working FreeRTOS + lwIP baseline on the NUCLEO-H563ZI (ping + UDP echo), recorded versions, and a frozen ISA v1.
- **M2:** a correct datapath. The VM runs inside ST's `HAL_ETH_RxLinkCallback`, plus the lwIP hook and a native-C baseline. A UDP flood to port 1900 is dropped while ping and echo keep working for 10 minutes.

Decisions:
- Start fresh from ST's `LwIP_UDPTCP_Echo_Server_Netconn_RTOS` (package `stm32h5-classic-coremw-apps`, tag v2.1.0).
- Move the folder to `~/Projects/MTP_XDP_STM32` at the end of session 1.
- Commit on `main`. Every message starts with `role1:`, `role2:` or `joint:`.

**Order:** Phase 0 → Phase 1 (hardware first) → Phase 2 fills the waiting time (installs, downloads) → Phases 3 → 4 → 5 → 6.
**Every step:** change → build → `make -C tests/host && make -C tests/host asan` → one commit.
**[you]** marks steps that need your hands: a login, sudo, a cable, System Settings. **[me]** marks Claude's steps. **[both]** we do together.

## Phase 0: Repo setup (joint, session 1)
- [x] 0.1 **[me] Start git.** `git init -b main`, `.gitignore`, first commit of the bundle as-is: `joint: import v0.1.2 handoff bundle`.
- [x] 0.2 **[me] Update CLAUDE.md:**
  - §2: attribution only.
  - §3: one shared Mac, base project.
  - §4: the real layout.
  - §5: which compiler is used for what.
  - §6 rule 5: ST's port/app files are editable with small marked changes.
  - Also the ROADMAP M1 line and the README quick start.
- [x] 0.3 **[me] Save this plan** as `docs/PLAN_M1_M2.md`.
- [ ] 0.4 **[you] Install tools:**
  - `brew install iperf3 tcpreplay`
  - a venv from the Python 3.12 that's already installed, plus `scapy pyserial matplotlib pandas`
  - STM32CubeIDE 2.1.0 from st.com (needs your myST login; take the newest 2.x if 2.1.0 isn't offered)
  - a USB-C Ethernet adapter
- [ ] 0.5 **[you] At the end of session 1:** quit Claude Code, move the folder in Finder to `~/Projects/`, then reopen Claude Code there. **[me]** then re-check `git status` and the host tests.

## Phase 1: M1 hardware baseline (role2)
- [x] 1.1 **[me] Download ST's package and read the real files.** (Done 28 Oct; findings in `docs/LOG.md`.)
  - What: shallow clone at tag v2.1.0 into `~/Projects/vendor/` (outside our repo, never edited), with only the 7 submodules we need.
  - From the real files:
    - paste the exact RX drain loop
    - confirm the `ethernetif.c` / FreeRTOS / lwIP values, including stack-size units (`sys_arch.c`)
    - read the HAL version macros
    - check how `HAL_ETH_ReadData()` treats later segments of a frame whose first segment wasn't linked
    - check `configENABLE_FPU = 0` against the build's float ABI
  - Findings go to `docs/LOG.md`.
- [ ] 1.2 **[you, ~5 min] Build ST's example once, unmodified.** In CubeIDE: import it from the clone and click Build.
  - **[me]** Then I try a headless build from the terminal.
  - Fallbacks: `make -C Debug` with CubeIDE's GCC, or you paste the console output.
- [ ] 1.3 **[me] Create `stm32_project/`, self-contained.**
  - Copy the example's `Core/`, `LWIP/` and `STM32CubeIDE/`.
  - Copy only the needed vendor folders into `Drivers/` and `Middlewares/`.
  - Rewrite the `PARENT-6-PROJECT_LOC` paths and add `VENDOR.md`.
  - Commit: `role2: import ST LwIP_UDPTCP_Echo_Server_Netconn_RTOS (coremw-apps v2.1.0) as stm32_project` (no logic changes).
- [ ] 1.4 **[me] Static IP.** 192.168.10.2/24, gateway 192.168.10.1, `LWIP_DHCP 0`, a boot banner on USART3. Commit: `role2: static IP 192.168.10.2, DHCP off`.
- [ ] 1.5 **[me] Bench scripts.**
  - `bench/serial_log.py`: pyserial with a timeout, writes a log file.
  - `bench/udp_echo_test.py`: N datagrams; reports loss and RTT min/avg/max; no sudo.
  - Commit: `role2: bench scripts for UART log and UDP echo test`.
- [ ] 1.6 **[both] First hardware run.**
  - **[you]** Plug in the ST-LINK USB and the Ethernet cable; set the adapter to 192.168.10.1/24 in System Settings.
  - **[me]** Flash, read the UART, ping, and run the UDP and TCP echo tests.
  - Raw output goes to `eval/raw/<date>_m1_baseline/` with the commit hash.
  - If it fails, I flash ST's unmodified build to compare.
- [ ] 1.7 **[me] `docs/PLATFORM.md`** with every version:
  - CubeIDE and its GCC, Programmer CLI, ST-LINK firmware
  - package and component versions
  - board revision, macOS, adapter, Homebrew arm-gcc, Python and packages

  Commit: `role2: record pinned platform versions`.

## Phase 2: M1 ISA v1 freeze (role1, done while waiting on installs)
- [x] 2.1 (ISA v1 **frozen** 28 Oct 2026; all six recommendations accepted) **`docs/ISA.md`**, written from `vm_isa.h`, `vm_interpreter.c` and `vm_verifier.c`. It covers:
  - the encoding and the opcode table
  - semantics, verifier rules and runtime checks
  - fault ⇒ DROP, the limits, and the D12 note

  Deck-vs-code differences (G2, G3) are stated. Open choices are listed for you to decide. Commit: `role1: ISA v1 specification`.
- [x] 2.2 **Host tests.**
  - all 256 opcodes: known ones accepted, unknown ones rejected
  - edge cases per opcode
  - one test per verifier error code

  Commit: `role1: host tests for every ISA v1 opcode and verifier rule`.

**M1 done when:** ping and UDP echo work on the board (1.6), host tests pass normally and with ASan, versions are recorded (1.7), and `ISA.md` is frozen.

## Phase 3: M2, VM side (role1, host only; first because the hooks use it)
- [ ] 3.1 **D11:** fill in `insns_executed` on every exit path, with tests.
- [ ] 3.2 **D9:** a `vm_slot` install helper in `firmware/VM_Engine/`.
  - `vm_slot_install()` runs `vm_verify()` and refuses programs that fail, or whose `ctx` doesn't match the hook (ISA v1 Q5).
  - It stores a `const vm_program_t * volatile`; `vm_slot_get()` reads it.
  - Pure C, with tests. The double-buffered live update stays in January.

## Phase 4: M2 datapath (role2)
- [ ] 4.1 **`xdp_dma_hook.[ch]`**
  - Delete our 3 HAL callbacks, our pool and `s_xSemaphore` (fixes D2, D3, D4, D6).
  - Expose `xdp_hook_run(frame, len)` and its counters.
  - Rename the counter (D8) and use `vm_slot`.
  - arm-gcc compile check against the pinned headers.
- [ ] 4.2 **`ethernetif.c` `HAL_ETH_RxLinkCallback`**
  - On the first segment, call `xdp_hook_run()`.
  - On DROP: `pbuf_free()` back to `RX_POOL` and return without linking.
  - Add `firmware/` to the CubeIDE build.
  - Decide whether `ETH_RX_BUFFER_SIZE` goes from 1000 to 1536.
- [ ] 4.3 **D5:** drain while `HAL_ETH_ReadData()` returns `HAL_OK`; call `netif->input` only for non-NULL `p`.
- [ ] 4.4 **D7:** `LWIP_HOOK_FILENAME` and `LWIP_HOOK_IP4_INPUT` in `lwipopts.h`. The lwIP hook uses `vm_slot`. Document D10.
- [ ] 4.5 **Native-C filter** `firmware/DDoS_Prototype/ddos_native.[ch]` (both offset variants), with a host differential test against the VM on thousands of random and truncated frames.
- [ ] 4.6 **`EXP_CONFIG` C0–C4** in `firmware/DDoS_Prototype/exp_config.h`. Init installs only what the config needs; the banner prints the config.
- [ ] 4.7 **Diagnostics.**
  - HardFault handler (CFSR/HFSR/MMFAR/BFAR plus the stacked PC and LR)
  - stack-overflow check level 2
  - `configASSERT` printing file:line
  - a 5-second stats line: counters, `ETH->DMACMFCR`, free `RX_POOL` buffers

## Phase 5: M2 hardware test (joint)
- [ ] 5.1 **[me] `bench/flood_udp.py`** (scapy; fixed rate and duration; logs what it sent).
- [ ] 5.2 **[both] Test.** I write `docs/TEST_M2.md` first.
  - C4 build.
  - **[me]** UART log plus continuous ping and UDP echo.
  - **[you]** sudo flood to port 1900 for 10 minutes; then a flood to another port (should PASS); then C2 for 2 minutes.
  - **Pass if:** the flood is dropped (counters), ping and echo keep working, there's no fault or assert, and the counters add up.
  - Logs go to `eval/raw/`. Functional checks only, not performance numbers.

## Phase 6: M2 role1, start G3
- [ ] 6.1 **`vm_verify_ex()`:** constant tracking in one forward pass. If every load base is constant, report `max_load_end`. Adversarial host tests. The interpreter fast path comes later (M3+).

## End of every session
Update the "Current status" section of CLAUDE.md, add a dated entry to `docs/LOG.md` (what changed / how it was verified / open questions) and update the statuses in `docs/KNOWN_ISSUES.md`.

## Verification
- **Host:** `make -C tests/host` and `make -C tests/host asan` after every change. The test count only grows.
- **Cross-compile:** arm-none-eabi-gcc `-Werror` builds of `VM_Engine` and `Network_Hooks` (the hooks against the pinned vendor headers).
- **Firmware:** the headless CubeIDE build has 0 errors and no warnings in our files.
- **Board:** report only what was observed (UART, ping, echo, flood logs), saved in `eval/raw/` with the commit hash and date.

## Risks to check, not assume
- CubeIDE 2.x on Apple silicon, and its headless build.
- `configENABLE_FPU = 0` with a hard-float build could corrupt FPU registers between tasks.
- ST's package ships HAL 1.6.0, but the audit used 1.7.0: re-run the hook compile check.
- Line-rate tests need a Linux PC (February).
