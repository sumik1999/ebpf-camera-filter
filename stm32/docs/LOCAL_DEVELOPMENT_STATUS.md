# Local comprehensive-filter development status

Branch: `stm32-comprehensive-local`

This branch is intentionally local until review. It is based on `stm32-new-filter`; do not push it without explicit approval.

## Milestone 1 implemented

- `VM_MAX_INSNS` is build-time configurable and defaults to 64.
- Two loader instruction banks now consume exactly 1,024 bytes instead of 8,192 bytes.
- The verifier, loader message limit, and storage arrays share the same bound.
- Compile-time assertions require the bound to fit `uint16_t` and the runtime step budget.
- Loader tests cover one instruction, exactly the selected limit, over-limit, zero-length, failed-install preservation, and A/B bank reuse.
- A build with `VM_MAX_INSNS=128` also passes, confirming configurability.

Host layout observations:

```text
VM_MAX_INSNS=64
instruction banks=1024 bytes
vm_loader_t=1072 bytes on the 64-bit host
packet_meta_t=36 bytes
```

The host `vm_loader_t` size includes 64-bit pointers and is not a Cortex-M33 RAM measurement. The target instruction-bank size is architecture-independent. Target `.text`, `.bss`, and stack recording remains open because the available Homebrew `arm-none-eabi-gcc` installation cannot currently find its C library headers.

## Parser foundation implemented

New files:

```text
firmware/Filter/packet_meta.h
firmware/Filter/packet_parser.h
firmware/Filter/packet_parser.c
tests/host/test_packet_parser.c
```

Current parser coverage:

- Ethernet II;
- multicast and broadcast destination detection;
- one VLAN tag and QinQ/two VLAN tags;
- strict rejection/unsupported result beyond two VLAN tags;
- ARP classification;
- explicit unsupported IPv6 classification;
- IPv4 version, IHL, total-length and truncation checks;
- IPv4 options;
- first and non-first fragment classification;
- UDP ports and length validation;
- TCP ports, flags and data-offset validation;
- minimum ICMP validation;
- explicit unknown IPv4 protocol classification.

The parser has no heap allocation and no unbounded loops. `packet_meta_t` is naturally aligned and currently 36 bytes on the host.

## Standalone stateless policy implemented (not integrated)

New files:

```text
firmware/Filter/filter_policy.h
firmware/Filter/filter_policy.c
firmware/Filter/ingress_filter.h
firmware/Filter/ingress_filter.c
tests/host/test_ingress_filter.c
```

The parser and policy are deliberately kept local and standalone. The production hook is restored to its original dynamic VM/native path:

```text
xdp_hook_run
  -> existing hook_run
  -> currently installed VM bytecode, native baseline, or PASS
```

`xdp_hook_run()` does not call `ingress_filter_run()`. This avoids a transitional native-C filter in front of the VM. The standalone native implementation is now the correctness oracle for the metadata bytecode that will be installed later.

The standalone monitor policy parses and counts every frame but passes everything. A validated strict policy can enforce:

- malformed and unsupported handling;
- explicit IPv6 and fragment behavior;
- untagged/VLAN allowlisting (including inner VLAN for QinQ);
- ARP, ICMP, TCP, UDP, other-L2 and other-IPv4 protocol policy;
- fixed TCP/UDP destination-service allowlists;
- exact PASS/DROP counters by reason.

Configuration is copied into fixed storage. Invalid counts, protocols, VLAN IDs, or zero service ports are refused without replacing the active policy. Tests confirm that a strict local policy can drop a frame while the untouched `xdp_hook_run()` still follows its installed VM/native program.

## Dynamic metadata VM implemented (still detached from production hook)

Implemented locally:

- canonical 32-byte, big-endian metadata ABI (`vm_meta_abi.[ch]`);
- ISA-v2 `VM_CTX_META` and bounded `VM_OP_CALL`;
- 37-instruction metadata policy equivalent to the native strict reference;
- native-versus-VM differential testing over 10,007 frames with zero mismatches;
- three immutable metadata program banks (active, previous rollback, staging);
- verification before a short callback-provided RTOS critical pointer swap;
- authenticated FMV2 packages with HMAC-SHA256, device ID and monotonic generation;
- built-in recovery policy and previous-generation rollback;
- fixed-depth management-task queue, with update work outside RX/ISR context;
- bounded global, service and 16-entry source rate helpers;
- assembler support for `ctx meta` and `call`, plus `filter_pack.py` packaging.

The existing `xdp_hook_run()` remains unchanged and continues to execute its legacy raw-frame slot.
`filter_manager_run()` is the complete metadata pipeline used by tests, but is not called by the
production hook until RTOS integration is explicitly approved.

## Verification

Default host build and ASan/UBSan build:

```text
329 passed, 0 failed
```

Tests include SHA-256/HMAC known answers, bad authentication, replay rejection, omitted helper
capabilities, inactive-bank loading, critical-section swaps, rollback, recovery, management queue,
rate burst/refill/tick wrap, and native/VM differential parity.

## Not implemented yet

- connection of `filter_manager_run()` to `xdp_hook_run()`;
- concrete FreeRTOS `taskENTER_CRITICAL` / `taskEXIT_CRITICAL` callback wiring;
- creation of the management task with `xTaskCreate` in board firmware;
- persistent flash A/B configuration across reboot;
- secure provisioning/storage of the HMAC key;
- strict ARP field validation;
- bounded IPv6 extension-header parsing;
- prefix rate table (global/service/source helpers exist);
- STM32 hardware measurements.

## Next coding slice

1. Review the metadata VM and update threat model.
2. Wire FreeRTOS critical and idle callbacks in the actual board application.
3. Create the management task and feed complete FMV2 packages from the authenticated control channel.
4. Add a build-time experiment mode that selects legacy or metadata dispatch without changing the hook signature.
5. Only then connect `filter_manager_run()` to the driver hook and measure its budget.
