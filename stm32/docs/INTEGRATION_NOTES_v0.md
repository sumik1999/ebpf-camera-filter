# Integration Notes (v0, Sep 2026)

> **Superseded in parts (28 Oct 2026 audit). Read `docs/KNOWN_ISSUES.md` first.**
> - Step 2 (CubeMX-generated `ethernetif.c`): CubeMX does **not** generate lwIP for STM32H5. Use ST's
>   `stm32h5-classic-coremw-apps`. Keep ST's RX callbacks and call our hook from inside them (D6).
> - Step 3 (`#include "lwip_ip4_hook.h"` in `lwipopts.h`) causes circular includes. Use
>   `#define LWIP_HOOK_FILENAME "lwip_ip4_hook.h"` instead (D7).
> - Section 3's ISR/task explanation is still correct and is the design to keep.

## 1. Directory layout

```
STM32_XDP_Project/
├── VM_Engine/
│   ├── vm_isa.h            (Role 1 - instruction format & opcodes)
│   ├── vm_verifier.h/.c    (Role 1 - static safety checks)
│   └── vm_interpreter.h/.c (Role 1 - bounded switch-case execution loop)
├── Network_Hooks/
│   ├── xdp_dma_hook.h/.c   (Role 2 - Ethernet DMA / ingress hook)
│   └── lwip_ip4_hook.h/.c  (Role 2 - lwIP ip4_input() baseline hook)
├── DDoS_Prototype/
│   ├── ddos_bytecode.h        (compiled bytecode array, both variants)
│   └── ddos_prototype_init.c  (verify + install at startup)
└── INTEGRATION_NOTES.md
```

Drop `VM_Engine/` and `Network_Hooks/` into your CubeIDE project as new
source groups (matching the target directory structure in the brief);
add both folders to the include path.

## 2. Wiring into an STM32CubeIDE / STM32H5 + FreeRTOS + lwIP project

1. **CubeMX**: enable ETH (RMII), FreeRTOS (CMSIS-RTOS v2 or native,
   both fine — the code above uses native FreeRTOS `SemaphoreHandle_t`
   directly), and LWIP middleware, targeting NUCLEO-H563ZI.
2. Your generated `ethernetif.c` already declares
   `s_xSemaphore` (name may differ slightly by CubeMX version — check
   `Middlewares/Third_Party/LwIP/system/OS/sys_arch.c` /
   `ethernetif.c` and adjust the `extern` in `xdp_dma_hook.c` to match).
   If CubeMX also generated its own `HAL_ETH_RxAllocateCallback` /
   `HAL_ETH_RxLinkCallback` / `HAL_ETH_RxCpltCallback` in
   `ethernetif.c`, **delete those three functions** there — this
   project supplies the single authoritative implementation in
   `xdp_dma_hook.c` (duplicate strong symbols will fail the link).
3. **lwipopts.h**: register the IPv4 hook non-invasively:
   ```c
   #include "lwip_ip4_hook.h"
   #define LWIP_HOOK_IP4_INPUT(pbuf, input_netif) \
               lwip_ip4_hook_input(pbuf, input_netif)
   ```
4. **main.c**, after `MX_LWIP_Init()`:
   ```c
   #include "ddos_prototype_init.h"   /* or just declare the prototype */
   ...
   ddos_prototype_init();
   ```

## 3. Why this stays deterministic: ISR ↔ FreeRTOS task interaction

The whole point of Role 1's bounded verifier + step budget is to make
the *cost* of running arbitrary bytecode predictable, so it can be
placed safely on a latency-critical path. How that predictability is
actually preserved end-to-end:

- **ETH_IRQHandler → `HAL_ETH_RxCpltCallback` (ISR context).**
  This callback does the absolute minimum: `xSemaphoreGiveFromISR()` +
  `portYIELD_FROM_ISR()`. It does **not** touch the VM. Interrupt
  latency is therefore independent of bytecode program size — the ISR
  cost is O(1) regardless of what filter is loaded.

- **`ethernetif_input` task (task context) → `HAL_ETH_ReadData()` →
  `HAL_ETH_RxLinkCallback` (the XDP hook).** The VM actually runs here,
  in task context, not in the ISR. This is deliberate: FreeRTOS ISRs
  on Cortex-M must return quickly to respect
  `configMAX_SYSCALL_INTERRUPT_PRIORITY` and avoid blocking
  higher-priority interrupts (SysTick, other peripherals). Running the
  VM in the `ethernetif_input` task instead means:
  - the ETH RX interrupt itself never has variable-length work in it;
  - the VM's cost is bounded by `VM_MAX_STEPS` and the verifier's
    forward-jump-only rule (Section 4 of `vm_verifier.c`), so the
    *task's* execution time per packet is also bounded — it cannot
    become an unbounded busy-loop that starves other tasks even from
    task context;
  - because the cost is bounded, you can assign `ethernetif_input` a
    priority high enough to keep up with line-rate RX (typically just
    below hard real-time control tasks, above general application
    tasks), and its worst-case execution time (WCET) is now something
    you can actually measure and budget for (`vm_run_stats_t.insns_executed`
    gives you a per-packet instruction count to log for the
    RTOS-timing-overhead comparison the brief asks for).

- **NVIC priority grouping.** Configure the ETH IRQ priority to be
  numerically ≥ `configMAX_SYSCALL_INTERRUPT_PRIORITY` (i.e. FreeRTOS
  API-safe) as CubeMX does by default when FreeRTOS is enabled, so
  `xSemaphoreGiveFromISR` is legal to call from it. Do not raise the
  ETH IRQ above that threshold unless you rewrite the hand-off to not
  use FreeRTOS APIs from the ISR.

- **DROP path costs strictly less than PASS.** On a DROP verdict,
  `xdp_dma_hook.c` returns the buffer to the pool immediately and does
  *no* pbuf allocation, no `pbuf_cat`, and the frame never enters
  lwIP's `tcpip_thread` mailbox at all. This is exactly the CPU/latency
  saving the project's motivation section claims ("saving CPU cycles
  and memory") and is what you'd graph against the lwIP-hook baseline,
  where a dropped packet has already paid for pbuf allocation *and* a
  full mailbox round-trip into `tcpip_thread` before `ip4_input()`
  ever runs the same check.

- **Fail-safe philosophy.** Every abnormal condition in the VM
  (unloaded program, OOB packet access, step-budget exceeded,
  allocation failure) resolves to DROP, never PASS. For a DDoS filter
  this is the correct default-safe behavior — a malformed or
  pathological bytecode program (which should never happen if the
  verifier ran first, but defense-in-depth costs one branch) cannot
  accidentally let attack traffic through.

## 4. Suggested benchmarking metrics (for the Role 2 evaluation)

| Metric | How to capture |
|---|---|
| Packet latency | Toggle a GPIO / DWT cycle counter at ISR entry and at hook verdict; compare DMA-hook vs. lwIP-hook vs. native C `if` chain doing the same check |
| Throughput | Count `rx_total` / `rx_passed` / `rx_dropped_vm` over a fixed window under flood traffic (e.g. `hping3 --udp -p 1900 --flood`) |
| CPU utilization | FreeRTOS runtime stats (`configGENERATE_RUN_TIME_STATS`) on `ethernetif_input` / `tcpip_thread` |
| Memory footprint | `.bss` size of `VM_Engine/` + `Network_Hooks/` objects (register file is stack-only, so static footprint is just the RX buffer pool + counters) |
| RTOS timing overhead | `vm_run_stats_t.insns_executed` per packet, plus task high-water-mark stack usage (`uxTaskGetStackHighWaterMark`) with vs. without the hook active |
