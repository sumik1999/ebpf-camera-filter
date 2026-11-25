# Partner handoff: STM32 programmable DDoS filter

## 1. Repository and branch

The combined project is available at:

- Repository: `https://github.com/sumik1999/ebpf-camera-filter.git`
- Working branch: `stm32-new-filter`
- STM32 project directory inside the repository: `stm32/`

Clone and enter the branch with:

```bash
git clone https://github.com/sumik1999/ebpf-camera-filter.git
cd ebpf-camera-filter
git switch stm32-new-filter
cd stm32
```

Do not develop this work on `main`. Pull the branch before starting:

```bash
git pull --ff-only origin stm32-new-filter
```

## 2. Imported project version

The STM32 tree was imported from:

- Original repository: `https://github.com/nishantsharma198/MTP_XDP_STM32.git`
- Original branch: `wifi-hook`
- Original commit: `aacf199211222f7aa9fa816c8c40d7aac783047e`

The original branch contains the complete Ethernet/QEMU implementation plus a Wi-Fi design proposal. Wi-Fi support has not yet been implemented.

Import details are also recorded in `IMPORT_PROVENANCE.md`.

## 3. Current state

The project currently provides:

- a compact, verified packet-filter bytecode VM;
- an early Ethernet-driver hook;
- a late lwIP hook for comparison;
- native-C and VM filter implementations;
- a Cortex-M33 QEMU harness;
- FreeRTOS 11.2.0 and lwIP 2.2.1 simulation;
- a simulated STM32 Ethernet DMA/RX path;
- runtime bytecode loading using XVM1 messages;
- double-buffered program replacement;
- traffic traces, experiment scripts and recorded evaluation results.

Run the host regression suite before making changes:

```bash
make -C tests/host
```

Expected result at handoff:

```text
209 passed, 0 failed
75,065 native-vs-VM verdict comparisons
0 mismatches
```

Also run the sanitizer build when changing parser, VM, loader or filter code:

```bash
make -C tests/host asan
```

## 4. Important limitation of the current filter

The existing DDoS demonstration is not yet a general rate-based DDoS defense. It drops every IPv4 UDP packet sent to destination port 1900.

Current rule:

```text
IPv4 AND UDP AND destination port 1900 -> DROP
anything else                           -> PASS
```

It currently has no:

- packet-per-second or byte-rate limiting;
- token buckets;
- per-source accounting;
- prefix accounting;
- TCP SYN-flood detection;
- adaptive thresholds;
- generalized service policy.

It also assumes a fixed IPv4 header size. VLAN packets, IPv4 options, fragments, IPv6 and TCP are not comprehensively handled.

The next stage is therefore a new comprehensive filter, not merely another blocked-port rule.

## 5. Target architecture

Keep the filter in three bounded layers.

### Layer 1: native parser

Parse packets once in trusted native C and produce a fixed `packet_meta_t` structure.

Required protocol coverage:

- Ethernet II;
- up to two VLAN tags;
- IPv4 with variable IHL;
- IPv4 fragmentation state;
- optional IPv6 with a bounded extension-header walk;
- UDP;
- TCP and TCP flags;
- ICMP;
- ARP;
- malformed and truncated packets.

The parser must contain no unbounded loops and must not allocate memory.

### Layer 2: programmable classifier

The verified VM should classify normalized packet metadata rather than repeatedly parsing fixed packet offsets.

Example policy classes:

- management;
- critical telemetry;
- ordinary application traffic;
- DNS/DHCP/NTP;
- UDP;
- TCP SYN;
- established TCP candidate;
- ICMP;
- ARP;
- fragments;
- malformed or unsupported traffic.

The VM should not be allowed to write packet memory or mutate arbitrary state.

### Layer 3: native enforcement engine

Use bounded native state for:

- total PPS and byte-rate limits;
- protocol-specific limits;
- service/port-specific limits;
- TCP SYN limits;
- ARP and ICMP limits;
- fragment limits;
- exact-source limits;
- source-prefix limits;
- reserved capacity for critical traffic.

This separation keeps the parser and state deterministic while preserving programmable classification.

## 6. Memory target

Target less than approximately 4 KB of filter RAM, excluding the existing Ethernet and lwIP pools.

Initial allocation proposal:

| Component | Approximate RAM |
|---|---:|
| Global/protocol/service buckets | 256–384 B |
| 32-entry source table | 512–768 B |
| 16-entry IPv4 `/24` table | 256–384 B |
| Policies and statistics | 512–768 B |
| Two 64-instruction VM banks | 1,024 B |
| Total | approximately 2.5–3.5 KB |

The current loader supports 512 instructions and reserves two 4 KB instruction banks per loader. The embedded default should be reduced to 64 instructions, giving two 512-byte banks.

## 7. Immediate implementation tasks

### Step 1: establish the baseline

1. Run normal and sanitizer tests.
2. Record current `.text`, `.data`, `.bss` and stack measurements.
3. Confirm that all generated evaluation data remains reproducible.
4. Do not mix new results with the old fixed-port baseline.

### Step 2: reduce loader RAM

1. Make the maximum instruction count configurable.
2. Use an embedded default of 64 instructions.
3. Keep all loader length and overflow checks.
4. Add boundary tests for 0, 1, 64 and 65 instructions.
5. Confirm that failed loads preserve the active program.

### Step 3: implement `packet_meta_t`

Suggested fields include:

```c
typedef struct {
    uint32_t src_ip4;
    uint32_t dst_ip4;
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t frame_len;
    uint16_t vlan_id;
    uint8_t  ip_version;
    uint8_t  protocol;
    uint8_t  tcp_flags;
    uint8_t  fragment_state;
    uint8_t  malformed;
    uint8_t  multicast;
} packet_meta_t;
```

The final layout should be packed for predictable size only if unaligned access is handled safely. Otherwise, order fields to obtain natural alignment.

### Step 4: parser tests

Add tests for:

- ordinary Ethernet/IPv4/UDP and TCP;
- minimum and maximum IPv4 IHL;
- one and two VLAN tags;
- first and non-first fragments;
- TCP SYN, SYN-ACK, ACK and invalid flags;
- ARP and ICMP;
- optional IPv6;
- every truncation boundary;
- inconsistent length fields;
- random malformed frames under ASan/UBSan.

Do not add rate limiting until parser tests are complete.

### Step 5: stateless policy

Add:

- allowed VLANs;
- allowed local protocols and destination ports;
- malformed-packet rejection;
- explicit fragment behavior;
- explicit IPv6 behavior;
- exact reason counters for every drop.

Preserve the current port-1900 rule as a regression configuration.

### Step 6: token buckets

Implement integer token buckets for:

- total packets;
- total bytes;
- UDP;
- TCP SYN;
- ICMP;
- ARP;
- each enabled service.

Use a monotonic RTOS tick, saturating counters and wrap-safe subtraction. Avoid floating point and division in the packet path.

### Step 7: source and prefix defense

Add fixed-size tables:

- 32 exact IPv4 source entries;
- 16 IPv4 `/24` prefix entries;
- two-way set-associative lookup;
- deterministic replacement;
- table-eviction counters.

A global bucket must remain in place because source-spoofed or widely distributed attacks can evade per-source limits.

### Step 8: programmable metadata classification

Prefer a new `VM_CTX_META` context. If VM semantics change, create ISA v2 and update these together:

- `docs/ISA.md`;
- `firmware/VM_Engine/vm_isa.h`;
- verifier;
- interpreter;
- loader;
- assembler;
- host and QEMU tests.

Do not silently change ISA v1 semantics.

## 8. Updating the filter without reflashing

The existing runtime path is:

```text
XVM1 message
    -> message-length and CRC checks
    -> decode into inactive RAM bank
    -> bytecode verification
    -> hook-context validation
    -> active-program pointer swap
```

The old program stays active if any validation step fails. Double buffering prevents the active program from being overwritten while packets are using it.

Two separate runtime update types should be supported:

1. **Policy update**
   - thresholds;
   - burst capacities;
   - enabled services;
   - VLANs;
   - protected management peers;
   - fragment and IPv6 behavior.

2. **Program update**
   - changes how packet metadata is classified;
   - is verified before activation;
   - should be less frequent than policy updates.

Most tuning should not require replacing bytecode.

## 9. Security required for live updates

The current UDP control path uses CRC only. CRC detects corruption but does not prove who sent the update. It must not be enabled as-is in a deployed device.

Introduce an authenticated update envelope containing:

- protocol magic and version;
- device or group identity;
- target hook and update type;
- monotonically increasing sequence number;
- payload length and hash;
- boot/session nonce or expiry;
- HMAC-SHA256 or a digital signature.

Update sequence:

1. Receive into a bounded staging buffer.
2. Reject oversized messages.
3. Authenticate the complete message.
4. Reject stale or replayed sequence numbers.
5. Decode into the inactive bank.
6. Verify bytecode or validate policy ranges.
7. Optionally test against a small safety corpus.
8. Atomically switch the active bank.
9. Return the active generation and hash.
10. Keep the previous bank for rollback.

Also:

- restrict updates to a management interface/VLAN;
- rate-limit the control service;
- compile a recovery policy into firmware;
- use two flash slots if configuration must survive reboot;
- limit flash writes to avoid wear;
- roll back after repeated watchdog resets or failed health checks.

## 10. Evaluation configurations

At minimum compare:

| Configuration | Filter | Placement |
|---|---|---|
| C0 | none | — |
| C1 | native comprehensive filter | lwIP hook |
| C2 | VM classifier + enforcement | lwIP hook |
| C3 | native comprehensive filter | driver hook |
| C4 | VM classifier + enforcement | driver hook |

Measure:

- correct PASS/DROP decisions;
- false positives and false negatives;
- legitimate traffic delivered during attack;
- instructions and hardware cycles;
- RAM, flash and stack usage;
- RX-pool exhaustion and DMA loss;
- policy-update interruption;
- update authentication and rollback behavior.

Clearly label QEMU instruction measurements and modeled throughput. Do not describe them as STM32 hardware measurements.

## 11. Traffic test matrix

Include:

- legitimate ARP, ICMP, DNS, DHCP, telemetry, management and application traffic;
- UDP floods with fixed and random sources/ports;
- large-packet and small-packet floods;
- TCP SYN, ACK, invalid-flag and connection-churn floods;
- ICMP and ARP floods;
- VLAN, IPv4 options, fragments and IPv6;
- malformed and truncated frames;
- threshold boundaries and bursts;
- RTOS tick wrap;
- source-table collisions and eviction;
- invalid signature/MAC, bad CRC, replay, downgrade, wrong context and interrupted updates.

## 12. Git workflow

Before each unit of work:

```bash
git switch stm32-new-filter
git pull --ff-only origin stm32-new-filter
make -C stm32/tests/host
```

Use small commits. Suggested prefixes:

```text
stm32-parser:
stm32-policy:
stm32-vm:
stm32-update:
stm32-tests:
stm32-docs:
```

After changes:

```bash
make -C stm32/tests/host
make -C stm32/tests/host asan
git status
git add <only the intended files>
git commit -m "stm32-parser: ..."
git push origin stm32-new-filter
```

Do not commit generated binaries, local toolchains, credentials, update keys or unrelated files from the parent Airport project.

## 13. Recommended first handoff assignment

The safest first assignment is:

1. reduce the loader maximum to 64 instructions;
2. prove the RAM reduction with the linker/size output;
3. implement `packet_meta_t` and an Ethernet + VLAN + IPv4 parser;
4. add exhaustive truncation and IPv4-IHL tests;
5. keep all 209 existing checks passing;
6. submit the changes as a small pull request before adding token buckets.

The complete design rationale and milestone definitions are in `docs/PLAN_COMPREHENSIVE_FILTER.md`.
