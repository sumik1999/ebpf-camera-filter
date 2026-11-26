# Dynamic metadata filter design

Status: implemented on local branch `stm32-comprehensive-local`, but deliberately not connected to `xdp_hook_run()` yet.

## Runtime path

```text
raw Ethernet frame
  -> packet_parse_ethernet()
  -> vm_meta_encode() (32-byte ABI v1)
  -> active VM_CTX_META program
  -> bounded rate helper(s)
  -> PASS or DROP
```

The existing Ethernet hook still executes the legacy raw-frame slot. Board integration will select one path, never chain both filters.

## Canonical metadata ABI

`firmware/Filter/vm_meta_abi.h` defines a 32-byte, padding-free context. Multibyte fields are big-endian so `LDPKT_H/W` can read them directly. Byte zero carries `VM_META_ABI_VERSION` and update manifests declare the required ABI.

## ISA v2

ISA v2 preserves ISA-v1 instruction encodings and adds:

- `VM_CTX_META`;
- `VM_OP_CALL` in class 7;
- bounded helpers `RATE_GLOBAL`, `RATE_SERVICE`, and `RATE_SOURCE`;
- `vm_interpreter_run_ex()` with a per-execution helper capability mask.

Raw Ethernet/IPv4 programs remain supported. A helper fault fails closed with DROP.

## Metadata program

`meta_filter_bytecode.h` contains a 37-instruction program equivalent to the strict native policy. It validates metadata status/version, fragments and VLAN, permits ARP and ICMP, permits UDP/53 and TCP/443, and calls a service rate helper before PASS.

The native policy remains a differential oracle. Host tests compare native and VM verdicts over structured and 10,000 deterministic random frames.

## FMV2 authenticated package

All integer fields are little-endian. The package is:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 | `FMV2` magic |
| 4 | 1 | package format (1) |
| 5 | 1 | ISA version (2) |
| 6 | 1 | context (`VM_CTX_META`) |
| 7 | 1 | metadata ABI version |
| 8 | 2 | instruction count |
| 10 | 2 | reserved |
| 12 | 4 | monotonic generation |
| 16 | 4 | device/group ID |
| 20 | 4 | helper capability mask |
| 24 | 4 | bytecode CRC-32 |
| 28 | 32 | HMAC-SHA256 |
| 60 | N×8 | bytecode |

The HMAC covers bytes 0–27 followed by bytecode. The key is not transmitted. CRC provides early corruption detection; HMAC authenticates all policy-relevant fields and bytecode.

`tools/asm/bcgen.py` assembles `ctx meta` and `call`. `tools/asm/filter_pack.py` creates FMV2 packages.

## Program banks and publication

`filter_manager_t` owns three fixed instruction banks:

1. active;
2. previous generation for rollback;
3. staging.

The update manager authenticates and verifies staging before entering a caller-provided RTOS critical section. The critical section contains only the aligned active-pointer store. RX reads the pointer once per packet.

The built-in recovery program remains in flash. Rollback swaps active and previous; recovery selects the built-in program. The highest accepted generation is retained across RAM rollbacks to prevent replay.

## Management task

`filter_management_task_t` provides a fixed two-message queue and FreeRTOS-compatible task entry. Submission copies a complete package in task context. HMAC, decoding and verification run in the management task, never in the ISR or packet RX path. The board must supply critical-section and idle/delay callbacks.

## Rate helpers

Native fixed state remains outside bytecode:

- one global token bucket;
- four service buckets;
- sixteen bounded source entries with two-probe lookup/replacement.

The VM can only call helper IDs allowed by both the authenticated package and execution environment. It receives an allow/deny result in r0 and cannot access table memory.

## Remaining board integration

- provision and protect the HMAC key;
- wire FreeRTOS critical callbacks and create the management task;
- feed complete FMV2 messages from an authenticated control transport;
- add persistent flash generations if updates must survive reboot;
- select metadata dispatch in `xdp_hook_run()` behind an experiment/build mode;
- measure stack, cycles and RX deadline on hardware.
