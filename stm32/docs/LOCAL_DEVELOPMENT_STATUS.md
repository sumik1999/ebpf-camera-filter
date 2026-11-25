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

## Verification

Default host build:

```text
279 passed, 0 failed
```

ASan/UBSan build:

```text
279 passed, 0 failed
```

Override build with `VM_MAX_INSNS=128`:

```text
279 passed, 0 failed
```

## Not implemented yet

- parser integration into the live Ethernet hook;
- strict ARP field validation;
- bounded IPv6 extension-header parsing;
- policy/service allowlists;
- token buckets;
- source and prefix tables;
- `VM_CTX_META` / ISA v2 classification;
- authenticated XCFG2 runtime policy updates;
- STM32 hardware measurements.

## Next coding slice

1. Add a native policy structure with explicit malformed, IPv6, fragment, VLAN and service behavior.
2. Add exact PASS/DROP reason counters.
3. Integrate parse + stateless policy into a new experiment configuration without changing C0–C4.
4. Add global PPS/BPS token buckets only after the stateless policy tests pass.
