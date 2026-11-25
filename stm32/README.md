# MTP: Lightweight Programmable Packet Processing on STM32 (handoff bundle, 28 Oct 2026)

Kiran (2025EET2800) · Nishant (2025EET2734) · Supervisor: Prof. Kaushik Saha
Board: NUCLEO-H563ZI (STM32H563ZIT6, Cortex-M33 @ 250 MHz) · FreeRTOS · lwIP

## Quick start
1. This folder (the one containing this README and `CLAUDE.md`) is your **project folder** and a
   git repository. Keep it at `~/Projects/MTP_XDP_STM32` (no spaces in the path; see `docs/MAC_SETUP.md` §1).
2. `stm32_project/` will be created from ST's `LwIP_UDPTCP_Echo_Server_Netconn_RTOS` example
   (see `docs/PLAN_M1_M2.md`, Phase 1).
3. Open this folder in Claude Code. Current work and next steps: `docs/PLAN_M1_M2.md` and `docs/LOG.md`.

The original first-session prompt is in `docs/KICKOFF_PROMPT.md` (Prompt A, already used on 28 Oct 2026).
On a Mac (e.g. MacBook Air M1), do `docs/MAC_SETUP.md` steps 1–4 first.

## What's in here

| Path | What it is |
|---|---|
| `CLAUDE.md` | Project memory for Claude Code. Keep it at the top of this folder (don't move it into a subfolder). |
| `docs/Project_Dossier.pdf` | **Start here.** Everything about the project in one document. |
| `docs/KICKOFF_PROMPT.md` | Paste-ready prompts for Claude Code (setup, Kiran, Nishant). |
| `docs/KNOWN_ISSUES.md` | Audited defects (D1–D12), filter limitations, deck-vs-code gaps (G1–G8). |
| `docs/ROADMAP.md` | Milestones with definitions of done, through the final thesis. |
| `docs/EVALUATION_PLAN.md` | Research questions, configurations C0–C6, metrics, methods. |
| `docs/LOG.md` | Session log. Append a dated entry each session. |
| `docs/MAC_SETUP.md` | macOS / Apple-silicon setup: tools, folder location, USB and Ethernet to the board, limits. |
| `docs/INTEGRATION_NOTES_v0.md` | Original v0 integration notes. Superseded where KNOWN_ISSUES says so (D7). |
| `docs/reference/` | What you shared: implementation guide PDF, the two role descriptions, the mid-term deck. |
| `firmware/VM_Engine/` | ISA, verifier, interpreter (v0.1: compiles, tested). |
| `firmware/Network_Hooks/` | DMA/driver hook (**v0: has blockers D2–D6**) and lwIP hook (compiles). |
| `firmware/DDoS_Prototype/` | DDoS bytecode (UDP port 1900) and startup install code. |
| `tests/host/` | 33 host unit tests: `make -C tests/host` (and `make -C tests/host asan`). |

## Changelog
- **v0.1.2 (28 Oct 2026):** added `docs/MAC_SETUP.md` (MacBook Air M1); Mac notes in CLAUDE.md and Prompt A.
- **v0.1.1 (28 Oct 2026):** clearer setup steps (project folder, `stm32_project/`, Prompt A placeholders, git step).
- **v0.1 (28 Oct 2026):** added `<stddef.h>` to `vm_isa.h` (VM engine didn't compile without it).
  Added host unit tests. Audited all code against the STM32H5 HAL v1.7.0, lwIP 2.2.x and
  FreeRTOS headers. Wrote the CLAUDE.md, docs and dossier.
- **v0 (Sep 2026):** initial VM engine, hooks, DDoS bytecode, integration notes.
