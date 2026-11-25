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

## Verification

Default host build and ASan/UBSan build:

```text
300 passed, 0 failed
```

The earlier override build with `VM_MAX_INSNS=128` passed all 279 tests available at that milestone, confirming VM storage configurability.

## Not implemented yet

- strict ARP field validation;
- bounded IPv6 extension-header parsing;
- token buckets;
- source and prefix tables;
- atomic policy replacement while RX is active;
- `VM_CTX_META` / ISA v2 classification;
- authenticated XCFG2 runtime policy updates;
- STM32 hardware measurements.

## Next coding slice

1. Freeze a canonical metadata byte layout independent of C padding.
2. Introduce `VM_CTX_META` as ISA v2 and teach the verifier/loader about it.
3. Manually translate the standalone stateless policy into metadata bytecode.
4. Differential-test native policy versus VM policy before installing it in the hook.
5. Integrate only the verified metadata program through the existing dynamic slot/update mechanism.
