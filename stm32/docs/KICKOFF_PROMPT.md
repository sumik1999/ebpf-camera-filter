# Prompts for Claude Code

## Before the first session (one-time setup)

1. **Unzip** `MTP_XDP_STM32.zip`. Open folders until you see one that *directly* contains
   `CLAUDE.md`, `README.md`, `docs/`, `firmware/`, `tests/`. **That folder is your project folder**
   (also called the "repo root"). Windows "Extract All" often makes a doubled folder
   (`MTP_XDP_STM32\MTP_XDP_STM32\`). If so, use the inner one.
2. **Bring your STM32CubeIDE project inside it.** First make a backup zip of your existing CubeIDE
   project (the folder with the `.ioc` file). Then move it into the project folder and name it
   `stm32_project/`. From now on, open it in CubeIDE from this new location
   (File → Import → Existing Projects into Workspace) so only one copy exists.
   No CubeIDE project yet? Skip this step; Claude Code will help you create one.
3. **Open the project folder in Claude Code** (select it as the local folder). Claude Code reads
   `CLAUDE.md` from the top of that folder automatically at the start of every session.
4. **Paste Prompt A** after filling in its three `<...>` lines. You don't need to attach the
   dossier PDF: it's already in `docs/`, and the prompt tells Claude to read it.
5. In later sessions, Kiran uses Prompt B and Nishant uses Prompt C.

---

## Prompt A: first session (whoever sets up the repo)

```
I'm continuing my MTech thesis project in this repository. Before doing anything, read
CLAUDE.md, docs/KNOWN_ISSUES.md, docs/ROADMAP.md and docs/EVALUATION_PLAN.md. Skim
docs/Project_Dossier.pdf and docs/reference/ for background.

I am: <Nishant (2025EET2734), Role 2: datapath and RTOS integration | Kiran (2025EET2800), Role 1: VM and execution layer>
My STM32CubeIDE project is at: <stm32_project/ inside this folder | another path | not created yet>
My computer: <e.g. MacBook Air M1, macOS (Apple silicon), see docs/MAC_SETUP.md | Windows | Linux>
Today's date: <date>

STEP 0: Discovery only, no edits yet. Report back:
 1. Repo state: git status, folder tree (2 levels), what is vendor vs our code.
 2. Does the STM32 project build? Which toolchain/IDE versions and which STM32H5 HAL version?
 3. Where lwIP comes from (ST's stm32h5-classic-coremw-apps or other). In ethernetif.c: which
    HAL_ETH_Rx* callbacks it defines, RX buffer pool name/size (RX_POOL, ETH_RX_BUFFER_CNT),
    the RX semaphore name, and the exact RX drain loop (copy it).
 4. FreeRTOS config: tick rate, priorities and stack sizes of the RX input task and tcpip_thread,
    configMAX_SYSCALL_INTERRUPT_PRIORITY, run-time stats on/off.
 5. Tools on this machine: arm-none-eabi-gcc, make, python3 (scapy, pyserial), tcpreplay or
    hping3, iperf3, STM32_Programmer_CLI, STM32CubeIDE headless build, serial-port access to the
    ST-LINK VCP. On a Mac, compare against docs/MAC_SETUP.md and list what's missing.
 6. Run `make -C tests/host` and `make -C tests/host asan` and show the summary lines.
    If make/gcc aren't installed on this machine, tell me how to install them.
 7. Is this folder a git repository? If not, propose setting one up (with a .gitignore for
    CubeIDE build output such as Debug/, Release/, .metadata/) so we have version history.
    Kiran and I will later share it via a private GitHub repo.
 8. If the actual folder layout differs from section 4 of CLAUDE.md, propose an update to it.
Then use plan mode to propose the plan for milestone M1/M2 (docs/ROADMAP.md) for MY role.
Wait for my approval before editing.

Working agreement for every session:
- Small steps: change -> build -> host tests -> commit (one logical change per commit,
  message says what and why). Don't edit vendor code (HAL, lwIP core, FreeRTOS kernel);
  use callbacks, hooks and CubeMX USER CODE sections.
- Follow the engineering and research-integrity rules in CLAUDE.md. Never invent numbers. Never
  claim hardware behaviour you didn't observe. If you can't flash or read the board from here,
  tell me exactly what to run and what to paste back.
- Respect code ownership in CLAUDE.md. Flag cross-boundary changes explicitly.
- At the end of the session: update the "Current status" section of CLAUDE.md, append a dated
  entry to docs/LOG.md (what changed, what was verified and how, open questions), and update
  docs/KNOWN_ISSUES.md statuses.
```

---

## Prompt B: Kiran's sessions (Role 1, VM and execution layer)

```
Role 1 session (Kiran). Read CLAUDE.md, docs/KNOWN_ISSUES.md, docs/ROADMAP.md first.
Goals for this session, in order. Stop and report after each:
 1. Write docs/ISA.md: the full ISA v1 spec (encoding diagram, every opcode with semantics,
    register conventions, verdict values, verifier rules, runtime checks). Keep it consistent with
    firmware/VM_Engine/vm_isa.h. If they disagree, ask me which is right.
 2. Fix D9 (install API verifies first, volatile program pointer, double-buffer swap helper)
    and D11. Add host tests for each.
 3. Verifier: static max-offset analysis (gap G3). For loads whose base register is provably the
    constant 0, compute the minimum frame length the program needs. Expose it in the verified
    program metadata so the hook can check the length once and the interpreter can skip per-load
    checks in a "pre-checked" fast path. Prove it with host tests, including adversarial ones.
 4. tools/asm/: Python assembler and disassembler (text -> vm_insn_t C array and .bin; .bin -> text),
    with label support for forward jumps and round-trip tests against tests/host.
 5. DDoS bytecode v1 in assembly: IHL-aware, TCP+UDP, several target ports, skips non-first IP
    fragments, optional 802.1Q VLAN. Update tests/host/test_vm.c "known limitations" tests to the
    new expected behaviour.
After each change: make -C tests/host && make -C tests/host asan, plus the arm-none-eabi
cross-compile check from CLAUDE.md. Report .text size and the .su stack numbers before and after.
```

---

## Prompt C: Nishant's sessions (Role 2, datapath and RTOS integration)

```
Role 2 session (Nishant). Read CLAUDE.md, docs/KNOWN_ISSUES.md, docs/ROADMAP.md,
docs/EVALUATION_PLAN.md first.
Goals for this session, in order. Stop and report after each:
 1. Hardware baseline (M1): FreeRTOS + lwIP on NUCLEO-H563ZI from ST's
    stm32h5-classic-coremw-apps, static IP, ping and UDP echo working. Record HAL/IDE versions
    in docs/PLATFORM.md.
 2. Fix D2-D7 from docs/KNOWN_ISSUES.md:
    - ST's ethernetif.c stays the owner of HAL_ETH_RxAllocateCallback / RxLinkCallback /
      RxCpltCallback. Remove our duplicate definitions.
    - Expose `xdp_verdict_t xdp_hook_run(const uint8_t *frame, uint16_t len)` from
      Network_Hooks/xdp_dma_hook.c. Call it inside ST's HAL_ETH_RxLinkCallback for the FIRST
      segment of a frame (when *pStart == NULL). On DROP: pbuf_free() that RX buffer's custom pbuf
      (returns it to RX_POOL) and return without linking. Later segments of a dropped frame are
      skipped by the HAL itself (verify this in stm32h5xx_hal_eth.c and note it).
    - Fix the RX drain loop to iterate on HAL_ETH_ReadData()'s return status, not on p != NULL.
    - Register the lwIP hook with LWIP_HOOK_FILENAME (never include lwIP headers from lwipopts.h).
    - Add the native-C version of the same filter for both hook points.
    - Compile-time switch EXP_CONFIG = C0..C6 (docs/EVALUATION_PLAN.md).
 3. Give me a hardware test procedure: what to flash, the PC commands (ping, UDP echo test,
    hping3 flood to UDP 1900, flood to another port), and what UART output to expect. I'll run it
    and paste results. Investigate any HardFault with the fault registers (CFSR/HFSR/MMFAR/BFAR).
 4. Measurement infrastructure (M3): DWT CYCCNT around the verdict, GPIO probe pins (list which
    Nucleo pins), FreeRTOS run-time stats, idle-hook CPU load, ETH->DMACMFCR, periodic UART stats
    as CSV lines. Then bench/ scripts that run a flood sweep and save CSVs tagged with the git
    commit hash.
```

---

## Handy one-liners for later sessions

- *"Re-read CLAUDE.md and docs/LOG.md, summarise where we are, and propose today's plan."*
- *"Audit the slides/report draft in <file> against the code and docs/KNOWN_ISSUES.md. List every claim the code doesn't support."*
- *"Generate the figures for RQ1–RQ4 from eval/raw/*.csv with eval/plot.py. Don't hand-edit numbers."*
- *"Act as a strict thesis examiner: ask me the 15 hardest questions about this design and grade my answers."*
