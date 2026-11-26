# Plan: comprehensive programmable STM32 ingress filter

Status: design proposal for branch `stm32-new-filter`.

## 1. Goal and constraints

Build a bounded-memory ingress defense for STM32H563 + FreeRTOS + lwIP that:

- rejects malformed and unsupported traffic before it consumes lwIP/RTOS resources;
- limits UDP floods, TCP SYN floods, ICMP/ARP floods, spoofed-source floods, and distributed floods;
- preserves explicitly configured management and application traffic under overload;
- permits authenticated policy and bytecode updates without reflashing or stopping packet reception;
- uses no heap in the packet path and has deterministic CPU and RAM bounds;
- runs at the driver hook, with the lwIP hook retained as a comparison baseline;
- remains testable on the host and QEMU before hardware validation.

This filter protects MCU CPU, packet buffers, and protocol state. It cannot prevent saturation of the physical Ethernet/Wi-Fi link; volumetric link protection still requires an upstream device.

## 2. Architecture

Do not put the entire defense inside bytecode. Split it into three bounded layers:

1. **Native parser and normalization**
   - Parse Ethernet, up to two VLAN tags, IPv4 with variable IHL, IPv6 with a bounded extension-header walk, TCP, UDP, ICMP, and ARP.
   - Detect truncation, invalid lengths, bad versions, and fragments.
   - Produce a fixed `packet_meta_t`; bytecode never depends on fragile fixed packet offsets.

2. **Programmable classifier**
   - Add a `VM_CTX_META` context whose loads read fields from `packet_meta_t`.
   - A verified program assigns a policy class, such as management, telemetry, DNS, ordinary UDP, TCP SYN, established TCP, ICMP, ARP, or unsupported.
   - Keep arbitrary packet writes and arbitrary state mutation out of the ISA.

3. **Native bounded enforcement engine**
   - Apply allow/deny policy and token buckets selected by the classifier.
   - Maintain global, per-protocol, per-service, per-source, and per-prefix limits.
   - Return PASS or DROP and update exact reason counters.

This split keeps parsing and state safe and efficient while preserving programmable policy selection.

## 3. Initial memory budget

Target less than 4 KB RAM for the active filter, excluding existing Ethernet/lwIP pools:

| Component | Proposed size |
|---|---:|
| Global/protocol/service token buckets | 256–384 B |
| 32-entry exact-source table | 512–768 B |
| 16-entry /24 prefix table | 256–384 B |
| Policy table, counters, configuration | 512–768 B |
| Two 64-instruction VM program banks | 1,024 B |
| Parser/classifier working state | stack only, target <256 B |
| Total target | approximately 2.5–3.5 KB |

Milestone 1 reduces `VM_MAX_INSNS` from 512 to a configurable embedded default of 64. The imported loader reserved 8 KB per hook; 64 instructions require only 1 KB for two banks. A larger test-only limit can still be selected at build time.

Use saturating 16/32-bit counters and tick-based arithmetic. Avoid per-bucket 64-bit timestamps, floating point, division, and dynamic allocation in the receive path.

## 4. Filter policy

Apply the following stages in order:

1. **L2 sanity and allowlist**
   - Permit only required destination MACs, broadcast uses, and configured VLANs.
   - Rate-limit ARP rather than allowing it without a bound.

2. **L3/L4 validation**
   - Drop malformed or truncated packets.
   - Enforce IPv4 total length and IHL.
   - Either drop fragments or impose a strict fragment budget; default to drop when the application does not need reassembly.
   - Disable/drop IPv6 if unused; otherwise use a bounded extension-header walk.

3. **Service allowlist**
   - Permit only configured local protocols and ports.
   - Reject traffic to closed services before it reaches lwIP.

4. **Global overload control**
   - Total PPS token bucket.
   - Total byte-rate token bucket.
   - Separate UDP, TCP-SYN, ICMP, ARP, and fragment buckets.

5. **Service limits**
   - Independent PPS/BPS and burst allowance for each exposed service.
   - Reserve capacity for authenticated management and essential telemetry.
   - Protected classes still have generous finite limits; there is no unlimited IP-based whitelist.

6. **Source and prefix limits**
   - Fixed 2-way set-associative source table, initially 32 entries.
   - Fixed prefix table, initially 16 IPv4 /24 entries.
   - Deterministic oldest/lowest-score replacement.
   - A global bucket catches fully distributed or spoofed-source attacks.

7. **TCP protection**
   - Rate-limit new SYN-without-ACK packets globally, by service, source, and prefix.
   - Reject invalid flag combinations.
   - Do not implement full connection tracking in the filter; let lwIP own established connection state.

8. **Decision and telemetry**
   - Count PASS/DROP by reason, protocol, policy class, and table-eviction count.
   - Export periodic snapshots outside the packet path.

## 5. Live filter changes

The repository already has the basic mechanism:

`XVM1 message -> CRC check -> inactive RAM bank -> verifier -> context check -> atomic slot pointer swap`

This changes bytecode without reflashing and keeps the old program active if validation fails. Before using it outside a demo, extend it as follows.

### 5.1 Two update types

- **Program update:** changes classification logic.
- **Policy update:** changes thresholds, bursts, enabled services, VLANs, protected peers, and default fragment/IPv6 behavior without changing bytecode.

Most operational changes should be policy updates; replacing bytecode should be less frequent.

### 5.2 Secure update envelope

CRC detects corruption but does not authenticate the sender. Define an `XCFG2` envelope containing:

- magic and format version;
- device/group ID;
- update type and target hook;
- monotonically increasing sequence number;
- payload length and payload hash;
- expiration or boot/session nonce;
- HMAC-SHA256 initially, or Ed25519/ECDSA if public-key provisioning is available.

Reject unauthenticated, stale, replayed, wrong-device, oversized, or context-mismatched updates before invoking the VM loader. Restrict the control service to a management VLAN/interface and apply a low PPS limit to it.

### 5.3 Transactional activation

1. Receive into a bounded staging buffer.
2. Authenticate the complete message.
3. Decode into the inactive program/policy bank.
4. Verify bytecode and validate every policy range.
5. Optionally dry-run against a small built-in safety corpus.
6. Enter a short critical section and atomically swap one active pointer/index.
7. Increment the active generation and report its hash.
8. Keep the previous bank for rollback.

A packet must use one generation from start to finish. The RX task reads the active pointer once at packet entry.

### 5.4 Recovery and persistence

- Compile a minimal recovery policy into flash.
- On boot, activate the recovery policy first.
- Persist only authenticated, validated configurations, using two flash slots with generation and CRC to tolerate power loss.
- Rate-limit flash writes to control wear.
- Roll back after repeated watchdog resets or a management-health timeout.
- Provide a physical/debug recovery path that ignores network-loaded policy.

## 6. Implementation milestones

### M1 — Baseline and resource budget

Status: **in progress locally**. The 64-instruction limit, 1 KB double banks, and boundary tests are implemented; target footprint recording and CI remain.

- Keep all imported checks green while adding parser tests.
- Use a build-time embedded VM instruction limit of 64.
- Record `.text`, `.bss`, loader RAM, and stack before changes.
- Add CI for host tests and sanitizers.

**Done when:** behavior is unchanged and the loader uses approximately 1 KB per hook rather than 8 KB.

### M2 — Safe normalized parser

- Add `packet_meta.[ch]` and table-driven parser tests.
- Cover VLAN, IPv4 options, fragments, IPv6, TCP flags, UDP, ICMP, ARP, truncation, and malformed lengths.
- Differential/fuzz test the parser under ASan/UBSan.

**Done when:** no current fixed-offset bypass remains and every parser loop has a strict bound.

### M3 — Stateless comprehensive policy

Status: **native reference implemented locally, not connected to the hook**. Service/VLAN/protocol policy and exact reason counters are covered by host and sanitizer tests. The driver hook remains on its original dynamically installed VM/native path until equivalent metadata bytecode passes differential tests.

- Add service/VLAN allowlists and malformed/unsupported handling. **Implemented as the native reference.**
- Rewrite the stateless decisions as verified `VM_CTX_META` bytecode. **Next.**
- Add global and per-service PPS/BPS token buckets. **After VM-policy parity.**
- Add exact reason counters and deterministic tick tests. **Native reason counters implemented; tick tests await token buckets.**

**Done when:** UDP, SYN, ICMP, ARP, fragment, and bandwidth floods are bounded while reserved traffic continues.

### M4 — Bounded source defense

- Add 32-entry source and 16-entry prefix tables.
- Test collisions, replacement, source spoofing, distributed attacks, saturation, and counter wrap.
- Tune sizes from measured RAM and traffic, not assumptions.

**Done when:** single-source and rotating-source floods are controlled with fixed RAM and runtime bounds.

### M5 — Programmable classifier

Status: **implemented locally, detached from the production hook.** ISA v2 adds `VM_CTX_META` and a bounded helper call. The canonical metadata policy is 37 instructions and matches the native reference over 10,007 differential inputs.

- `VM_CTX_META`, verifier, interpreter, assembler and tests are updated together.
- ISA-v1 Ethernet/IPv4 programs and XVM1 loading remain supported.
- Metadata programs cannot write memory; helper IDs and manifest capability masks are bounded.

**Done when:** board integration executes the verified metadata program without bypassing parser safety.

### M6 — Secure live updates

Status: **RAM-only core implemented locally.** FMV2 uses HMAC-SHA256, device ID, monotonic generation, CRC, helper capabilities, three program banks, recovery and rollback. A bounded management-task queue keeps authentication and verification out of RX/ISR context.

- Unauthenticated UDP/9000 remains legacy simulation code and must be disabled in production.
- Concrete FreeRTOS task/critical callback wiring and protected key provisioning remain.
- Persistent flash slots and power-loss tests remain.

**Done when:** invalid updates never replace the active policy and replayed/unauthorized updates are rejected on the board control path.

### M7 — Evaluation

Compare at least:

- no filter;
- native comprehensive filter at lwIP;
- native comprehensive filter at driver ingress;
- VM classifier + native enforcement at driver ingress.

Measure correctness, false positives, delivered legitimate traffic, instructions/cycles, stack, RAM/flash, RX-pool pressure, update interruption, and rollback time. Clearly separate QEMU/model results from NUCLEO-H563ZI hardware results.

## 7. Test matrix

Include:

- legitimate ARP, ICMP, DNS, DHCP, telemetry, management, TCP, and application traffic;
- UDP fixed-source, random-source, random-port, and large-packet floods;
- TCP SYN, ACK, invalid-flag, and connection-churn floods;
- ICMP, ARP, fragments, VLAN, IPv4 options, IPv6, malformed and truncated frames;
- threshold boundaries, burst refill, tick wrap, table collisions and eviction;
- authenticated update, bad MAC/signature, bad CRC, replay, wrong device/context, downgrade, interruption, and rollback;
- mixed attack/legitimate traces at several burst sizes and rates.

## 8. First coding slice

Start with M1 and M2, not stateful bytecode:

1. reduce/configure the loader program limit;
2. add `packet_meta_t` and an Ethernet/IPv4/UDP/TCP parser;
3. replace the fixed-offset native filter with metadata-based service classification;
4. preserve the existing filter as a regression baseline;
5. add token buckets only after parser tests are complete.
