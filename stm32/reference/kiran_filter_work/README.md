# Kiran's filter work — partner reference snapshot

This directory is the exact code snapshot to use when studying Kiran's existing filter work before implementing the comprehensive STM32 filter.

## Where to find it

- Repository: `https://github.com/sumik1999/ebpf-camera-filter.git`
- Branch: `stm32-new-filter`
- Directory: `stm32/reference/kiran_filter_work/`

Commands:

```bash
git clone https://github.com/sumik1999/ebpf-camera-filter.git
cd ebpf-camera-filter
git switch stm32-new-filter
cd stm32/reference/kiran_filter_work
```

This is a reference snapshot. New STM32 implementation work belongs under `stm32/firmware/`, with tests under `stm32/tests/`; do not develop the STM32 port inside this reference directory.

## Filters included

### 1. Count-Min Sketch XDP filter

File:

```text
ebpf/xdp_cms_filter.bpf.c
```

Behavior:

- Parses Ethernet, IPv4, UDP and TCP.
- Processes UDP packets and TCP SYN-without-ACK packets.
- Uses four rows of 1,024 counters.
- Estimates a flow's packet rate as the minimum row count.
- Drops traffic above `DEFAULT_THRESHOLD_PPS`.

Supporting definitions:

```text
ebpf/common.h
ebpf/maps.h
```

Use this as a reference for bounded approximate flow counting. Do not port its 4,096 timestamped buckets directly to STM32; the STM32 design must use a smaller table and avoid a 64-bit timestamp in every bucket.

### 2. ElasticSketch-inspired XDP filter

File:

```text
ebpf/xdp_elastic_filter.bpf.c
```

Behavior:

- Uses a light shared sketch for new flows.
- Promotes detected heavy flows into an LRU per-flow table.
- Maintains precise counts for promoted flows.
- Drops heavy UDP and TCP SYN traffic.

Use this as a reference for separating heavy and light traffic. The current implementation is intentionally simplified and its one-row light sketch can produce collision-based false positives. The STM32 design should use deterministic fixed-size tables rather than an LRU map.

### 3. RAPID port-anomaly guard

File:

```text
ebpf/rapid_port_guard.bpf.c
```

Supporting code:

```text
config/rapid_topology.json
eval/rapid_adaptive_controller.py
scripts/attach_rapid_dataplane.sh
scripts/attach_rapid_adaptive.sh
scripts/attach_rapid_network.sh
```

Behavior:

- Runs at TC ingress and egress on bridge ports.
- Tracks receive/transmit packet imbalance per port.
- Evaluates after a time trigger or packet-count trigger.
- Blocks controller-designated untrusted edge ports after an anomaly.
- Uses fixed BPF maps for port state, guarded ports and blocked ports.
- Reports decisions through a ring buffer.

Use this as a reference for topology-aware mitigation and for combining a fast dataplane with a slower controller. It is not a per-packet STM32 filter and should not be copied literally into MCU firmware.

### 4. nftables baseline

Files:

```text
filters/nftables_guard.sh
filters/clear_nftables.sh
```

These provide a conventional Linux filtering baseline for comparison.

## Experiment and traffic code

The snapshot also contains:

- `traffic/`: UDP flood, SYN flood, camera service and telemetry tools;
- `eval/`: result collection, controller tests, simulations and visualizations;
- `scripts/`: namespace topology, XDP attachment, RAPID attachment and experiment runners;
- `config.env`: Linux experiment configuration;
- `Makefile`: build and experiment entry points;
- `buildroot-package/`: Buildroot packaging for the two XDP filters;
- `docs/`: compilation and RAPID notes.

Generated experiment results, binaries, personal files and unrelated report material are deliberately excluded.

## What should be reused for STM32

Reuse the concepts, not Linux-specific APIs:

| Existing concept | STM32 equivalent |
|---|---|
| XDP/TC early hook | Ethernet driver RX hook before lwIP |
| BPF maps | Fixed static C arrays/tables |
| `bpf_ktime_get_ns()` | Wrap-safe FreeRTOS or hardware tick |
| CMS counters | Small saturating counter sketch, if measurements justify it |
| Heavy-flow LRU table | Fixed 2-way set-associative source table |
| RAPID controller policy | Bounded native policy engine and management task |
| XDP PASS/DROP | Existing VM/native hook PASS/DROP verdict |
| User-space rule change | Authenticated XCFG2 policy/program update |

## Known weaknesses to avoid in the STM32 design

1. The Linux filters primarily cover IPv4 UDP and TCP SYN floods.
2. VLAN, IPv6, IPv4 options and fragments are not comprehensively handled.
3. The CMS and heavy-flow counters use non-atomic updates on multicore Linux.
4. A fixed 400 PPS threshold is compiled into the program.
5. The current IP-based telemetry exemption can be bypassed by source spoofing.
6. Per-source accounting alone can be evaded by distributed or spoofed sources.
7. The Elastic light table is only one row.
8. These filters use packet rate but do not consistently enforce byte rate.
9. Linux BPF map sizes are too large for a direct low-memory STM32 port.

The new implementation should instead use the architecture in:

```text
stm32/docs/PLAN_COMPREHENSIVE_FILTER.md
stm32/docs/PARTNER_HANDOFF_NEXT_STEPS.md
```

## Partner's first task

After reading the three filter implementations:

1. run `make -C stm32/tests/host` from the repository root;
2. reduce the STM32 VM loader default from 512 to 64 instructions;
3. implement and test normalized `packet_meta_t` parsing;
4. preserve these Linux filters unchanged as comparison/reference material;
5. add token buckets only after parser boundary tests pass.

When describing this work, distinguish clearly between:

- Kiran's Linux/XDP/CMS/Elastic/RAPID reference filters in this directory;
- the imported STM32 programmable VM in `stm32/firmware/`;
- the new comprehensive STM32 filter that will be implemented on this branch.
