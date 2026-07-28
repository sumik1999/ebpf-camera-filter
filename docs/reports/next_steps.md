# Next Steps: DDoS Mitigation Project Handoff

**Source plan:** `docs/references/DDoS_Mitigation_Project_Plan_FINAL.pdf`

**Branch:** `qemu-raspberry-testing`

**Target:** Raspberry Pi 4 Camera OS, ARM64, Buildroot

**Audience:** New engineer continuing the implementation

**Purpose:** Prioritized implementation plan, repository map, acceptance criteria, and validation checklist

## 1. Executive summary

The repository already contains a working Buildroot Camera OS, a Raspberry Pi 4 QEMU environment, four loopback and WPA2 software-camera tests, a hardened shared packet parser, an XDP-to-TC pipeline, deterministic and probabilistic blocklists, basic CMS and ElasticSketch-inspired classifiers, and a bounded nftables trust policy.

The next engineer should not rebuild those foundations. The next unimplemented PDF requirement is the **adaptive SYN completion-ratio detector**. After that, the main work is to improve TC analytics, move thresholds into runtime maps, add telemetry and an adaptive controller, complete application policy, deploy automatically at boot, run on physical Raspberry Pi hardware, and execute the formal Week 11 benchmark matrix.

A practical completion estimate is:

| Scope | Current state | Remaining work |
|---|---|---|
| Functional research prototype | Approximately 60–65% | SYN detector, analytics, controller, telemetry |
| Production-oriented prototype | Approximately 40–50% | Policy, boot integration, hardware, tuning, evaluation |

## 2. Existing baseline—do not reimplement

The following capabilities are already implemented and regression-tested:

- Dockerized QEMU 10 Raspberry Pi 4 boot environment.
- Four deterministic prerecorded camera inputs.
- Four-camera concurrent loopback video test.
- WPA2 access point with four isolated `mac80211_hwsim` camera stations.
- Shared Ethernet, dual-VLAN, IPv4, TCP, and UDP parser.
- Explicit malformed-frame and over-depth VLAN decisions.
- True five-tuple flow keys including source port.
- Dedicated XDP and genuine TC SCHED_CLS programs.
- Simultaneous XDP validation followed by TC CMS or Elastic analytics.
- Exact IPv4 hash deny list.
- IPv4 LPM prefix deny list.
- 8,192-bit, three-hash Bloom filter.
- Pinned blocklist maps that persist across XDP reload.
- Camera OS `airport-blocklist` management command.
- Removal of the spoofable APOC eBPF bypass.
- Interface-aware source-prefix checks and bounded nftables telemetry allowance.
- Buildroot packaging for all five BPF objects and the blocklist utility.

Baseline regression commands:

```bash
make validate
./scripts/qemu/camera-network-test.sh
./scripts/qemu/wifi-camera-test.sh
```

Both camera tests must continue to pass after every phase below.

## 3. Priority 1—adaptive SYN completion-ratio detector

### 3.1 Goal

Implement the PDF requirement to classify sources using the ratio of completed TCP handshakes rather than raw SYN volume. A legitimate burst with successful SYN/SYN-ACK/ACK completion must not be treated the same as a spoofed SYN flood.

### 3.2 Important architecture constraint

The current XDP front end is attached to guard terminal ingress. It can observe client SYN and final ACK packets entering from the terminal side, but a camera SYN-ACK enters the guard through the camera-facing interface. Completion tracking therefore requires visibility on **both guard ingress interfaces** with shared maps, or an equivalent explicitly documented bidirectional hook design.

Recommended design:

```text
terminal ingress XDP                     camera ingress XDP
client SYN ───────────────┐         ┌──── camera SYN-ACK
client final ACK ─────────┼─────────┤
                          ↓ shared pinned maps
                  pending handshakes
                  source statistics
                  prefix statistics
                  runtime policy
```

### 3.3 Required state

Use bounded maps and monotonic kernel timestamps. Suggested state:

- Pending handshake LRU hash keyed by client/server addresses and ports.
- Per-source counters: SYN observed, SYN-ACK observed, completed ACK, failed/expired.
- Per-prefix counters for distributed attacks.
- Last-window timestamp and penalty-until timestamp.
- Runtime minimum sample count.
- Completion-ratio threshold.
- Window, pending timeout, cooldown, and recovery values.

Do not drop solely because one SYN has no immediate completion. Enforce only after a minimum sample count and a bounded observation window.

### 3.4 Packet semantics

The shared parser must distinguish:

- Initial SYN: `SYN=1, ACK=0`.
- SYN-ACK: `SYN=1, ACK=1`.
- Final ACK: `SYN=0, ACK=1`, matched to pending state.
- RST: close or penalize pending state according to documented policy.
- Retransmitted SYN: update safely without artificially inflating failure counts.

Sequence-number validation is optional for the first bounded prototype, but tuple and direction matching are mandatory. Document the residual risk of spoofed ACKs if sequence validation is omitted.

### 3.5 Files to manipulate

| Path | Action |
|---|---|
| `ebpf/include/packet_parser.h` | Extend TCP flag classification without weakening existing bounds checks |
| `ebpf/include/maps.h` | Add bounded handshake, source-statistics, and runtime-policy structures/maps |
| `ebpf/xdp/frontend.bpf.c` | Perform SYN/SYN-ACK/ACK state transitions and early decisions |
| `scripts/deployment/attach-xdp.sh` | Attach/reuse shared maps on both guard ingress interfaces |
| `scripts/deployment/detach.sh` | Detach both interfaces while preserving intended pinned state |
| `scripts/network/setup.sh` | Ensure both interfaces and routes expose the required directions |
| `config/defaults.env` | Add safe prototype window, ratio, sample, and cooldown defaults |
| `qemu/guest/tests/wifi-camera.sh` | Add successful-handshake and failed-handshake decision tests |
| `scripts/experiments/run.sh` | Collect SYN detector counters and identify the active policy |
| `buildroot/package/airport-ebpf/airport-ebpf.mk` | Package changed objects and future controller if added |

Suggested new files:

```text
ebpf/include/syn_detector.h
traffic/tcp_handshake_client.py
traffic/syn_completion_test.py
```

### 3.6 Acceptance criteria

- All BPF objects pass the ARM64 kernel verifier.
- XDP attaches to both required guard interfaces with compatible shared maps.
- Normal TCP clients complete connections with no unexpected drops.
- A spoofed or non-completing SYN workload crosses the configured minimum sample count and is penalized.
- A high-rate workload with a healthy completion ratio remains accepted within service limits.
- Penalties expire and the source recovers after cooldown.
- Source-prefix aggregation detects a distributed low-rate test.
- Four-camera loopback and WPA2 tests remain green.

## 4. Priority 2—complete Count-Min Sketch analytics

### 4.1 Goal

Extend the current packet-count prototype to meet the PDF requirement for fixed-memory estimates of:

- Packets per second.
- Bytes per second.
- Connection attempts.
- Approximate unique destination ports.
- Individual source rate.
- Source-prefix rate.
- Destination-camera pressure.

### 4.2 Implementation notes

Use separate metrics or clearly separated rows so unlike quantities are not added together. Add source and prefix aggregation in addition to the existing five-tuple identity. Unique-port estimation should use a bounded bitmap or another verifier-friendly approximation rather than an unbounded per-port map.

Counter updates must be multicore-safe. Prefer supported atomic operations where possible. If `bpf_spin_lock` is used, keep it in eligible map values and never hold it across helper calls.

### 4.3 Files to manipulate

| Path | Action |
|---|---|
| `ebpf/include/maps.h` | Define metric-specific CMS values and aggregation keys |
| `ebpf/tc/common.h` | Add shared time-window, byte-count, and aggregation helpers |
| `ebpf/tc/cms_filter.bpf.c` | Implement packet, byte, attempt, port, source, and prefix estimates |
| `ebpf/include/common.h` | Keep only context-independent helpers and structures |
| `eval/sketch_collision_demo.py` | Model collision and false-positive behavior for proposed sizes |
| `eval/collect_metrics.py` | Collect and label every CMS metric |
| `config/defaults.env` | Add prototype dimensions and safe limits |
| `scripts/deployment/attach-tc.sh` | Report selected dimensions and attachment state |

Suggested new files:

```text
ebpf/include/cms.h
eval/cms_accuracy.py
```

### 4.4 Acceptance criteria

- Controlled packet and byte counts remain within a documented error bound.
- Randomized source ports do not evade source-level aggregation.
- Multiple low-rate flows to one camera trigger destination pressure accounting.
- Multicore stress does not produce counter corruption or verifier rejection.
- Memory use is fixed and documented for Raspberry Pi configurations.

## 5. Priority 3—finish ElasticSketch-inspired heavy/light separation

### 5.1 Goal

Replace the simplified light side and promotion logic with an explicitly documented heavy/light algorithm that protects low-rate telemetry from collisions caused by heavy attack flows.

### 5.2 Required behavior

- Heavy bucket stores candidate identity and positive/negative votes.
- Light side uses multiple rows or another documented fixed-memory estimate.
- Promotion transfers the correct estimate.
- Replacement and demotion are deterministic and bounded.
- Window expiration prevents stale heavy hitters from remaining forever.
- Packet and byte metrics are represented consistently.

### 5.3 Files to manipulate

| Path | Action |
|---|---|
| `ebpf/include/maps.h` | Replace or version heavy/light value structures |
| `ebpf/tc/common.h` | Add reusable hashes, windows, and safe update helpers |
| `ebpf/tc/elastic_filter.bpf.c` | Implement promotion, voting, replacement, and expiration |
| `eval/sketch_collision_demo.py` | Compare CMS and Elastic collision behavior |
| `eval/visual_attack_sim.py` | Visualize heavy/light classification over time |
| `eval/collect_metrics.py` | Record promotions, replacements, heavy hits, and light estimates |

Suggested new file:

```text
ebpf/include/elastic.h
```

### 5.4 Acceptance criteria

- Heavy flows are isolated with a known promotion threshold.
- Legitimate low-rate telemetry remains below the false-positive target.
- Heavy-table churn and light collisions are measurable.
- CMS and Elastic use the same replay workloads for comparison.

## 6. Priority 4—runtime configuration and adaptive threshold engine

### 6.1 Goal

Replace compile-time-only thresholds with runtime maps and a resource-aware controller, as required by the PDF.

### 6.2 Controller inputs

- Recent legitimate baseline using EWMA or another documented estimator.
- XDP and TC packet/drop rates.
- CPU utilization.
- Available memory.
- Camera FPS, latency, and dropped frames.
- SYN completion ratio.
- CMS/Elastic heavy-flow pressure.

### 6.3 Controller safeguards

- Hard minimum and maximum thresholds.
- Hysteresis to prevent oscillation.
- Cooldown between updates.
- Safe defaults when telemetry is unavailable.
- Atomic generation or version change for related settings.
- Audit log of every threshold change and reason.
- Fail-open/fail-closed behavior documented per service.

### 6.4 Files to manipulate

| Path | Action |
|---|---|
| `ebpf/include/maps.h` | Add versioned runtime configuration and health maps |
| `ebpf/xdp/frontend.bpf.c` | Read active XDP policy without hard-coded limits |
| `ebpf/tc/cms_filter.bpf.c` | Read CMS runtime thresholds |
| `ebpf/tc/elastic_filter.bpf.c` | Read Elastic runtime thresholds |
| `scripts/deployment/blocklistctl.py` | Either extend safely or keep blocklist concerns separate |
| `config/defaults.env` | Define controller bounds, EWMA, hysteresis, and interval defaults |
| `scripts/lib/load-config.sh` | Continue loading defaults and private local override |
| `buildroot/package/airport-ebpf/Config.in` | Select runtime dependencies |
| `buildroot/package/airport-ebpf/airport-ebpf.mk` | Install controller and configuration |

Suggested new files:

```text
scripts/deployment/defense-controller.py
config/controller.defaults.env
buildroot/board/rpi4-camera/rootfs-overlay/etc/init.d/S42airport-defense
```

### 6.5 Acceptance criteria

- Thresholds change without recompiling or detaching BPF.
- Controller cannot set values outside hard safety bounds.
- Restart preserves or safely reconstructs configuration.
- CPU pressure lowers expensive analysis before camera service collapses.
- Stable traffic does not cause threshold oscillation.

## 7. Priority 5—kernel telemetry and sampled events

### 7.1 Goal

Make every important decision observable without logging one event per packet.

Required counters:

- Packets and bytes observed.
- XDP pass and malformed drop.
- Exact, prefix, and Bloom matches.
- SYN detector pass, penalty, expiration, and recovery.
- TC CMS and Elastic decisions.
- Current thresholds and configuration generation.
- Map allocation/update failures where observable.

Use a ring buffer only for sampled or state-change events. Counters should be per-CPU where suitable and aggregated in userspace.

### 7.2 Files to manipulate

| Path | Action |
|---|---|
| `ebpf/include/maps.h` | Add reason counters and ring-buffer event schema |
| `ebpf/xdp/frontend.bpf.c` | Increment exact XDP reason counters and sample events |
| `ebpf/tc/cms_filter.bpf.c` | Increment CMS reasons |
| `ebpf/tc/elastic_filter.bpf.c` | Increment Elastic reasons |
| `eval/collect_metrics.py` | Export stable JSON/CSV field names |
| `eval/summarize_results.py` | Include detection and false-positive summaries |
| `scripts/experiments/run.sh` | Start/stop collector with each run |

Suggested new files:

```text
scripts/deployment/telemetry-agent.py
docs/guides/TELEMETRY-SCHEMA.md
```

## 8. Priority 6—complete nftables and application policy

### 8.1 Goal

Implement stateful service policy for traffic that survives XDP and TC.

Required policy categories:

- Established and related traffic.
- Invalid-state rejection.
- RTSP TCP ports and approved clients.
- ONVIF control and discovery.
- MQTT or telemetry service ports if enabled.
- Camera management access.
- ICMP limits.
- New-connection limits.
- Interface/source-prefix anti-spoofing.
- Rate-limited logging.
- Dynamic timeout sets synchronized with defense decisions where appropriate.
- Explicit IPv6 decision: supported policy or disabled with documentation.

### 8.2 Files to manipulate

| Path | Action |
|---|---|
| `policy/nftables/apply.sh` | Expand service ACLs, timeout sets, logging, and counters |
| `policy/nftables/clear.sh` | Remove only project-owned policy safely |
| `config/defaults.env` | Add service ports, approved prefixes, and log limits |
| `traffic/camera_service.py` | Expose representative services and health behavior |
| `traffic/telemetry_client.py` | Validate legitimate policy |
| `traffic/telemetry_server.py` | Validate server-side policy and latency |
| `scripts/experiments/run.sh` | Select and capture policy mode |

Suggested new tests:

```text
traffic/onvif_probe.py
traffic/rtsp_probe.py
traffic/mqtt_probe.py
```

## 9. Priority 7—fragments, IPv6, and blocklist lifecycle

### 9.1 IPv4 fragments

Current validated fragments pass XDP and skip TC flow accounting because later fragments have no transport header. Add an explicit policy:

- Per-source fragment rate accounting.
- Maximum fragment rate.
- Overlap/tiny-fragment handling where feasible.
- Service-specific fragment allowance or rejection.

### 9.2 IPv6

Either implement bounded IPv6 parsing and policy or explicitly disable IPv6 on protected interfaces. Production deployment must not leave an unfiltered alternate protocol path.

### 9.3 Blocklist lifecycle

Add:

- Entry expiration.
- Generation metadata.
- Atomic active-generation switching for bulk updates.
- Bloom rebuild workflow.
- Source and audit metadata in userspace.
- False-positive measurement before Bloom enforcement.

### 9.4 Files to manipulate

| Path | Action |
|---|---|
| `ebpf/include/packet_parser.h` | Fragment policy and bounded IPv6 parsing |
| `ebpf/include/blocklist.h` | Expiration/generation semantics and optional IPv6 maps |
| `ebpf/xdp/frontend.bpf.c` | Apply explicit fragment/IPv6 decisions |
| `scripts/deployment/blocklistctl.py` | Bulk load, expiry, generation switch, audit output |
| `policy/nftables/apply.sh` | Match the documented IPv6 policy |
| `qemu/guest/tests/wifi-camera.sh` | Add fragment, IPv6, expiry, and Bloom rebuild tests |

## 10. Priority 8—automatic Camera OS deployment

### 10.1 Goal

Turn manually invoked tools into a boot-managed, recoverable defense service.

Boot workflow:

1. Mount bpffs.
2. Discover configured interfaces.
3. Load or reuse compatible pinned maps.
4. Attach native XDP when supported, otherwise generic XDP.
5. Attach selected TC classifier.
6. Apply nftables policy.
7. Start telemetry and adaptive controller.
8. Record health status.
9. Roll back safely if attachment or policy fails.

### 10.2 Files to manipulate

| Path | Action |
|---|---|
| `buildroot/package/airport-ebpf/Config.in` | Add deployment feature selections |
| `buildroot/package/airport-ebpf/airport-ebpf.mk` | Install service scripts and defaults |
| `buildroot/board/rpi4-camera/rootfs-overlay/etc/init.d/` | Add ordered defense startup/shutdown scripts |
| `buildroot/board/rpi4-camera/rootfs-overlay/etc/` | Add non-secret production defaults |
| `scripts/deployment/attach-xdp.sh` | Make interface discovery and fallback production-safe |
| `scripts/deployment/attach-tc.sh` | Make attachment idempotent |
| `scripts/deployment/detach.sh` | Implement safe service shutdown |
| `policy/nftables/apply.sh` | Make policy installation idempotent |

Acceptance requires repeated reboot testing, attachment health checks, and a documented rollback path.

## 11. Priority 9—camera stack and physical Raspberry Pi deployment

### 11.1 Application validation

The QEMU video files validate encode/decode and network behavior but are not camera devices. Physical validation must cover:

- CSI camera detection and device-tree configuration.
- `libcamera`/`rpicam` capture.
- GStreamer pipeline.
- RTSP server.
- ONVIF control/discovery.
- Hardware encoding where available.
- Concurrent defense and video processing.

### 11.2 Hardware validation

Test:

- Raspberry Pi 4 boot from generated SD image.
- GENET Ethernet and `brcmfmac` Wi-Fi.
- Native and generic XDP capabilities.
- TC and nftables behavior.
- Real camera capture.
- CPU frequency, temperature, and throttling.
- Sustained power and storage stability.
- 512 MB–2 GB resource profiles where hardware is available.

### 11.3 Files and folders to manipulate

```text
buildroot/configs/rpi4_camera_ebpf_defconfig
buildroot/board/rpi4-camera/linux-camera-ebpf.fragment
buildroot/board/rpi4-camera/config.txt
buildroot/board/rpi4-camera/rootfs-overlay/
buildroot/package/airport-ebpf/
docs/guides/SETUP-GUIDE.md
docs/reports/SECURITY-CAMERA-BUILD-REPORT.md
```

Suggested new files:

```text
scripts/hardware/check-camera.sh
scripts/hardware/check-network-hooks.sh
scripts/hardware/soak-test.sh
docs/guides/HARDWARE-VALIDATION.md
```

Do not modify unrelated upstream Buildroot packages unless a reproducible package defect requires a documented patch.

## 12. Priority 10—formal Week 11 experimentation

### 12.1 Defense cases

Run the same workload against:

1. No defense.
2. nftables only.
3. XDP plus TC CMS.
4. XDP plus TC Elastic.
5. Each defended mode with adaptive controller enabled and disabled.

### 12.2 Attack matrix

- SYN flood with no completion.
- High-rate legitimate completed connections.
- UDP flood.
- Randomized source-port UDP flood.
- Distributed low-rate source flood.
- Source-prefix flood.
- Reflection-style response traffic.
- Fragment flood.
- Known exact-IP and prefix blocks.
- Bloom membership and controlled false-positive trials.
- Mixed attack plus four camera streams.

### 12.3 Metrics from the PDF

| Category | Required metrics |
|---|---|
| Network | Packets/sec, throughput, RTT, jitter |
| Security | Detection accuracy, false positives, false negatives |
| Kernel | CPU cycles/packet, XDP execution time |
| Memory | Map/sketch size, cache misses where measurable |
| Camera | FPS, frame latency, dropped frames |
| System | CPU utilization, RAM use, boot time, temperature on hardware |

### 12.4 Files to manipulate

| Path | Action |
|---|---|
| `scripts/experiments/run.sh` | Stable run metadata and complete metric collection |
| `scripts/experiments/stress-matrix.sh` | Expand cases, repetitions, rates, and random seeds |
| `scripts/experiments/visual-demo.sh` | Keep demonstration separate from scientific results |
| `traffic/syn_flood.py` | Add reproducible modes and actual-send accounting |
| `traffic/udp_flood.py` | Add distributed/random-port/replay modes |
| `traffic/telemetry_client.py` | Measure legitimate delivery, RTT, and jitter |
| `traffic/camera_service.py` | Export application and frame-health metrics |
| `eval/collect_metrics.py` | Produce complete per-run JSON and CSV |
| `eval/summarize_results.py` | Confidence intervals and comparison tables |
| `eval/visual_attack_sim.py` | Plots derived from saved raw results only |
| `eval/results/` | Generated results; keep raw and summary layouts documented |

Suggested new files:

```text
traffic/reflection_replay.py
traffic/distributed_flood.py
eval/experiment_manifest.schema.json
eval/analyze_detection.py
eval/plot_performance.py
docs/reports/EXPERIMENTATION-REPORT.md
```

Use multiple repetitions, fixed random seeds, synchronized clocks, warm-up periods, and separate simulation claims from physical-hardware claims.

## 13. Repository manipulation map

### 13.1 Core implementation

| Folder | Responsibility |
|---|---|
| `ebpf/include/` | Shared parser, maps, blocklists, and future detector/sketch headers |
| `ebpf/xdp/` | Earliest validation, deny lists, SYN detector, XDP telemetry |
| `ebpf/tc/` | CMS and Elastic flow analytics |
| `policy/nftables/` | Stateful service ACL and logging layer |
| `scripts/deployment/` | Attach, detach, map control, future controller/agent |
| `config/` | Public safe defaults and ignored local overrides |

### 13.2 Tests and simulation

| Folder | Responsibility |
|---|---|
| `scripts/network/` | APOC, attacker, guard, and camera namespace topology |
| `scripts/experiments/` | Repeatable attack/defense runs |
| `traffic/` | Legitimate clients, services, and attack generators |
| `eval/` | Metrics, analysis, plots, and generated results |
| `scripts/qemu/` | Host-side QEMU build, boot, and regression entry points |
| `qemu/guest/` | Guest-side camera, parser, Wi-Fi, XDP, TC, and blocklist tests |

### 13.3 Embedded deployment

| Folder | Responsibility |
|---|---|
| `buildroot/package/airport-ebpf/` | Build and install project BPF and userspace tools |
| `buildroot/configs/rpi4_camera_ebpf_defconfig` | Canonical Camera OS package selection |
| `buildroot/board/rpi4-camera/` | Kernel fragment, boot config, rootfs overlay, startup policy |
| `containers/` | Reproducible Buildroot and QEMU container definitions |
| `scripts/build/` | Cross-platform image build entry points |

### 13.4 Documentation

| Folder | Responsibility |
|---|---|
| `docs/guides/` | Setup, operation, telemetry, and hardware procedures |
| `docs/reports/` | Branch, experiment, performance, and final reports |
| `docs/references/` | Original project plan and research source documents |

## 14. Files that should normally not be manipulated

- `output-images/`: generated local artifacts; rebuild instead of editing manually.
- `buildroot/output/`: generated Buildroot state; never commit it.
- `qemu/assets/*.y4m.gz`: regression fixtures; change only with corresponding expected-result updates.
- Unrelated files inside the vendored `buildroot/` upstream tree.
- `docs/references/DDoS_Mitigation_Project_Plan_FINAL.pdf`: source requirement document; preserve unchanged.
- `config/local.env`: machine-local and ignored; never use it as the canonical project configuration.
- Any private key, token, password, or production Wi-Fi credential.

## 15. Safe engineering workflow

For each feature:

1. Create one focused branch or commit.
2. Add structures with explicit sizes and padding.
3. Recreate incompatible pinned maps intentionally and document the ABI change.
4. Compile all five BPF objects through the Buildroot package.
5. Load every changed object into the target kernel verifier.
6. Add packet-decision tests before enabling drops.
7. Run both camera regression tests.
8. Capture baseline and defended metrics.
9. Update documentation and report limitations honestly.
10. Commit generated reports only when their source data is also retained.

Validation commands:

```bash
make validate

docker run --rm -v "$PWD:/build" -w /build airport-camera-builder \
  bash -c 'cd buildroot && make airport-ebpf-dirclean && make airport-ebpf'

./scripts/qemu/camera-network-test.sh
./scripts/qemu/wifi-camera-test.sh
```

## 16. Suggested first-week handoff plan

### Day 1—orientation

- Read the source PDF and the QEMU branch report.
- Run both QEMU camera tests unchanged.
- Read the shared parser and XDP front end.
- Inspect pinned maps with `bpftool` inside the guest test.

### Day 2—detector data model

- Define handshake keys, values, windows, and configuration.
- Calculate exact map memory use.
- Decide bidirectional hook placement.
- Write packet-decision test cases before enforcement.

### Days 3–4—bounded implementation

- Implement SYN, SYN-ACK, ACK, timeout, and recovery transitions.
- Load through the ARM64 verifier after every small change.
- Add counters before adding drop decisions.

### Day 5—enforcement and regression

- Enable minimum-sample completion-ratio enforcement.
- Test successful clients, spoofed floods, retransmission, RST, cooldown, and prefix aggregation.
- Run four-camera loopback and WPA2 regressions.
- Report measured effects and unresolved risks.

## 17. Definition of project completion

The plan can be considered implemented when:

- XDP performs hardened parsing, deny-list lookup, and adaptive SYN validation.
- TC produces bounded, multicore-safe CMS and Elastic heavy/light analytics.
- nftables enforces documented stateful camera-service policy.
- Thresholds adapt at runtime within hard safety bounds.
- Every decision has stable counters and sampled diagnostics.
- The full pipeline starts automatically and recovers safely on Camera OS.
- Real RTSP/ONVIF/video traffic survives representative attacks.
- Raspberry Pi 4 plus camera-module deployment passes soak and thermal tests.
- The formal attack matrix reports all PDF metrics with reproducible data.
- Final documentation distinguishes simulation results from physical-hardware results.

The immediate next coding task is the adaptive SYN completion-ratio detector. Start with telemetry-only state tracking, verify both traffic directions, and enable dropping only after the completion estimator and recovery behavior are proven by tests.
