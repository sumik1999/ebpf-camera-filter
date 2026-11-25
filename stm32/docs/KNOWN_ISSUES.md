# Known issues and gaps (audit of 28 Oct 2026)

How this list was produced: the v0 code was compiled on a host (gcc) and for Cortex-M33
(arm-none-eabi-gcc 13.2), unit-tested on the host (33 tests, also under ASan/UBSan), and the
network hooks were compiled against the real STM32H5 HAL (v1.7.0), lwIP 2.2.x and FreeRTOS-Kernel
headers from their GitHub repos. Items marked **confirmed** were reproduced; others come from reading
the vendor source.

Severity: **Blocker** = won't build or crashes · **Major** = wrong behaviour or skews results ·
**Minor** = cleanup / clarity.

**Last updated:** 24 Nov 2026 (final sprint; everything in emulation, no board). Previously 28 Oct 2026, session 1. D2/D5/D6 were rechecked against the pinned vendor package
(`stm32h5-classic-coremw-apps` v2.1.0: HAL 1.6.0, lwIP 2.2.1, FreeRTOS 11.2.0). D9 was raised to
Major, D12 is documented, and D13/D14 are new. Details: `docs/LOG.md`. "Attr." = thesis attribution
(CLAUDE.md §2).

## A. Code defects

| ID | Sev. | Status | Attr. | File | Problem | Fix |
|---|---|---|---|---|---|---|
| D1 | Blocker | **Fixed in v0.1** | Kiran | `VM_Engine/vm_isa.h` | `NULL` used without `<stddef.h>`, so the VM engine did not compile (confirmed). | Added `#include <stddef.h>`. |
| D2 | Blocker | **Fixed 24 Nov 2026** by the hook rewrite: our code no longer defines any HAL callback (`tools/xc_check.sh` compiles it) | Nishant | `Network_Hooks/xdp_dma_hook.c` | `HAL_ETH_RxAllocateCallback` declared as returning `HAL_StatusTypeDef`; the H5 HAL declares it `void` → *conflicting types* compile error (**confirmed**). | Return `void`; signal failure by leaving `*buff = NULL`. |
| D3 | Blocker | **Fixed 24 Nov 2026** (code removed: ST's `RX_POOL` owns the buffers) | Nishant | `xdp_dma_hook.c` | `pbuf_alloced_custom(..., NULL /* struct pbuf_custom* */, ...)`: lwIP writes through that pointer → NULL dereference / HardFault on the first PASS frame. | Use ST's design: each RX buffer lives in an `RxBuff_t { struct pbuf_custom pc; uint8_t buff[]; }` from `RX_POOL`; the pbuf is initialised at allocation time. |
| D4 | Blocker | **Fixed 24 Nov 2026** (code removed: ST's `RX_POOL` owns the buffers) | Nishant | `xdp_dma_hook.c` | No `custom_free_function`, so PASSed buffers never return to the pool. The pool is also only `ETH_RX_DESC_CNT` (4) deep, so **RX stops after ~4 PASS frames**. | Set `pc.custom_free_function` → return to pool. Size the pool `ETH_RX_BUFFER_CNT` (e.g. 12) > descriptor count, since lwIP holds buffers. |
| D5 | Major | **Fixed in the simulated driver, 24 Nov 2026** (`RX_LOOP_FIXED`), and **demonstrated**: with ST's loop and early drops, C4 answers only 55 % (2 frames per wake-up) / 29 % (4) of legit echoes vs 100 % with the fix (`eval/figures/fig4_d5_rx_loop.png`). Confirmed 28 Oct by reading ST v2.1.0 `ethernetif.c` and HAL 1.6.0. | Nishant | ST `ethernetif.c` RX loop | On DROP, `HAL_ETH_ReadData()` returns `HAL_OK` with `NULL`. ST's `do { p = low_level_input(); ... } while (p != NULL);` stops draining → frames stranded until the next IRQ → latency spikes for legit traffic, skewed measurements. | Drain on the **ReadData return status** rather than on `p`, e.g. `while (HAL_ETH_ReadData(&heth,(void**)&p) == HAL_OK) { if (p) netif->input(p, netif) ...; }` |
| D6 | Major | **Fixed 24 Nov 2026**: `xdp_hook_run(frame, len)` is a pure function; the call site for ST's `HAL_ETH_RxLinkCallback` is documented in `xdp_dma_hook.h` | Nishant | `xdp_dma_hook.c` | v0 re-defines all three RX callbacks that ST's `ethernetif.c` already defines → multiple-definition link errors. Also `extern s_xSemaphore` does not exist in ST's code (it uses `RxPktSemaphore`). | Keep ST's `ethernetif.c` as the owner. Inside its `HAL_ETH_RxLinkCallback`, call one function: `xdp_verdict_t xdp_hook_run(const uint8_t *frame, uint16_t len)`. On DROP: `pbuf_free(&rxbuff->pc.pbuf)` (returns to `RX_POOL`) and return without linking. |
| D7 | Major | **Fixed 24 Nov 2026** in the hook: `lwip_ip4_hook.h` includes no lwIP header and documents the two `lwipopts.h` lines; `tools/xc_check.sh` compiles lwIP's `ip4.c` with them | Nishant | `docs/INTEGRATION_NOTES_v0.md` | Notes say to `#include "lwip_ip4_hook.h"` inside `lwipopts.h` → circular include errors (**confirmed**). | `#define LWIP_HOOK_FILENAME "lwip_ip4_hook.h"` plus the hook macro (**confirmed to compile**, incl. lwIP `ip4.c`). |
| D8 | Minor | **Fixed 24 Nov 2026** (`hook_stats_t.rx_no_filter_pass`) | Nishant | `xdp_dma_hook.c` | Counter `rx_dropped_no_prog` is incremented when **no** program is loaded, but those frames PASS. Misleading name. | Rename to `rx_no_prog_pass`. |
| D9 | **Major** (was Minor) | **Fixed in VM_Engine 24 Nov 2026**: `vm_slot_install()` verifies, checks `ctx`, keeps the old program on refusal; volatile pointer (13 host tests). Hooks switch to it with the hook rewrite (plan step 3). | both | `*_set_program()` | Install APIs accept unverified programs; the pointer isn't `volatile`. Why Major: the interpreter does **not** range-check register indices (only the verifier does), so an unverified program with a register ≥ 8 reads/writes past its register array on the C stack (`docs/ISA.md` §8). Today every install path verifies first (`ddos_prototype_init.c`), but nothing enforces it. | `set_program()` calls `vm_verify()` and refuses on error; store as `const vm_program_t * volatile`. Use double-buffering for runtime updates. Also refuse a program whose `ctx` doesn't match the hook (ISA v1 Q5: `VM_CTX_ETH` for the driver hook, `VM_CTX_IPV4` for the lwIP hook). |
| D10 | Minor | **Documented 24 Nov 2026** in `lwip_ip4_hook_glue.c` (a load past `p->len` faults → DROP) | Nishant | `lwip_ip4_hook.c` | VM sees only the first pbuf segment (`p->len`). Fine for header fields with default buffer sizes; document it. | Document; optionally check `p->len >= needed`. |
| D11 | Minor | **Fixed 24 Nov 2026** (every exit goes through `vm_finish()`; 7 host tests) | Kiran | `vm_interpreter.c` | `insns_executed` is only filled on the RET path; error paths report 0. | Set it on every exit path. |
| D12 | Minor | Documented (`docs/ISA.md` §8, with a host test that the budget stops an *unverified* loop) | Kiran | `vm_interpreter.c` | Runtime step budget (2000) can never trigger for verified programs (forward-only ⇒ ≤ 512 steps). Harmless defence-in-depth, but say so in the thesis. | Keep; document. |
| D13 | Major | **Fixed in our FreeRTOSConfig.h (simulation, 24 Nov 2026)**; not on hardware | Nishant | ST example `Core/Inc/FreeRTOSConfig.h` (v2.1.0) | `configENABLE_FPU` is 0, but the CubeIDE project builds with `-mfloat-abi=hard`. The FreeRTOS CM33 port then does not save FPU registers s16–s31 on a context switch, so two tasks that use floating point can corrupt each other. Found by reading the config; not observed on hardware. | Set `configENABLE_FPU 1` in our copy, in its own commit right after the import (plan 1.3). |
| D14 | Minor | Open (simulation keeps ST's 1000-byte buffers; first-segment limit documented in `xdp_dma_hook.h`) | Nishant | ST example `LWIP/Target/ethernetif.c` (v2.1.0) | `ETH_RX_BUFFER_SIZE` is 1000, so frames longer than 1000 B use 2 DMA descriptors and the driver hook sees only the first 1000 B. If the hook DROPs a frame's first segment, HAL 1.6.0 skips its later segments and leaves their buffers on the descriptors for reuse (read in `HAL_ETH_ReadData()` / `ETH_UpdateDescriptor()`), so nothing leaks. | Decide in plan 4.2: raise it to 1536 (one descriptor per frame; 12 buffers × 512 B = 6 KB more RAM) or document the first-segment limit. |
| D15 | Minor | Observed in simulation 24 Nov 2026 | Nishant | ST `ethernetif.c` design + lwIP reassembly | Under FreeRTOS with 4 frames per RX wake-up and **no filter (C0)**, IP fragments held for reassembly plus frames queued in the tcpip mailbox use all 12 `RX_POOL` buffers (19 refill failures in the `odd` trace). C3/C4 never exhaust the pool (junk is dropped before it holds a buffer). | Report as a result (early drop protects the RX buffer pool). Optionally cap reassembly (`IP_REASS_MAX_PBUFS`). |

## B. DDoS bytecode limitations (asserted by `tests/host/test_vm.c`)

- **UDP only.** TCP to the target port PASSES.
- **Fixed offsets:** assumes IHL = 5. IPv4 options (IHL > 5) make an attack frame PASS.
- **No VLAN (802.1Q) parsing:** tagged frames PASS unfiltered.
- **IP fragments:** non-first fragments have no UDP header. Bytes at the "port" offset could
  match by chance (false DROP) or attackers could fragment to evade.
- Runt/truncated frames are DROPPED by the OOB fail-safe. That's acceptable, but state it.

## C. Mid-term deck vs implementation gaps (fix before the next evaluation)

| ID | Deck says | Reality | Action |
|---|---|---|---|
| G1 | Slides 6–7: DMA hook runs "inside HAL_ETH_RxCpltCallback interrupt", execution context "Hardware Interrupt (ISR)". | The VM runs in **task context** in `HAL_ETH_RxLinkCallback` (called by `HAL_ETH_ReadData()` from the RX task). The ISR only signals. | Describe it accurately ("driver layer, before lwIP, deferred from the ISR into the high-priority RX task"). Optionally add a VM-in-ISR variant as an *experiment*. |
| G2 | Slide 4: "fixed 512-byte stack". | The VM has **no stack memory**, only 8 × 32-bit registers (the interpreter's C frame is 72–80 B). 512 B is rBPF's/eBPF's stack. | Either add a verified 512-B scratch stack (ISA v1) or change the wording. |
| G3 | Slide 5: verifier "validates that packet loads do not exceed the raw packet length". | Packet length is only known at run time, so that check is **in the interpreter**. | Say "static + runtime checks", or implement static max-offset analysis (verifier computes the minimum frame length; hook checks once, then per-load checks are removed). Good thesis contribution. |
| G4 | Slide 8: drops "TCP, UDP packets". | Bytecode is UDP-only. | Extend the bytecode (ISA supports it) or fix the wording. |
| G5 | Slide 10: "bare-metal RTOS on a Cortex-M3". | Cortex-**M33**. | Typo fix. |
| G6 | Slide 7: DMA hook "Memory Allocation: None". | With ST's zero-copy driver, the custom-pbuf header is allocated together with every RX buffer from `RX_POOL` at refill time. The real saving is the skipped `tcpip_thread` mailbox hand-off and context switch, `ethernet_input()`, and earlier buffer recycling. | Rephrase precisely and **measure** it. |
| G7 | Slide 9: "Python assembler" (Phase 4). | Not started. | It's in the roadmap. |
| G8 | Slide 4: "computed jump table (switch-case)". | GCC compiles the `switch` to a single `TBH` jump table (confirmed in disassembly). A true *computed-goto* (labels-as-values) dispatcher does not exist yet. | Keep "jump-table switch". Add computed-goto as an optimisation **and benchmark both**. |
| G9 | "Rules can be updated at run time without reflashing" (the XDP/eBPF motivation). | **Addressed in simulation 24 Nov 2026:** `vm_loader` (XVM1 messages, CRC, verify via `vm_slot`, double-buffered) + UDP control port 9000 in the simulated firmware + `tools/asm/bcgen.py`. Demo `loader_demo` (C4): a broken rulebook is refused, a port-53 rulebook is installed mid-traffic, verdicts change with no rebuild. **Not authenticated** (anyone who can send UDP to the device can load a verified program). Not on hardware. | Say "the design supports run-time installation through `vm_slot`; a loading channel is future work". Don't claim live updates were demonstrated. |
