# QEMU Raspberry Testing Branch Report

**Branch:** `qemu-raspberry-testing`

**Parent branch:** `raspberry-updated`

**Report date:** 2026-07-28

**Target:** Raspberry Pi 4 Camera OS, ARM64, Buildroot

**Status:** Functional development branch with committed QEMU, Wi-Fi, and security enhancements

## 1. Executive summary

This branch now provides a repeatable Raspberry Pi 4 QEMU environment for validating the current Camera OS image before physical deployment. It boots the generated SD image, injects four deterministic camera recordings, validates video processing over loopback, and simulates a WPA2 camera network using software-defined Wi-Fi radios.

The eBPF design has also advanced from separate XDP-only prototypes to a functional multi-layer pipeline:

```text
Ingress interface
    ↓
XDP front end
    ├── malformed frame                 → XDP_DROP
    ├── exact/prefix/Bloom deny match   → XDP_DROP
    └── accepted IPv4                   → XDP_PASS
                                            ↓
                                      TC ingress
                                      ├── CMS, or
                                      └── ElasticSketch-inspired accounting
                                            ↓
                                      nftables policy
                                            ↓
                                      camera services
```

The branch has passed QEMU boot, four-camera loopback, WPA2 association, XDP attachment, TC attachment, parser, blocklist, map-persistence, and video-regression tests. It is not yet production complete: adaptive SYN detection, improved sketches, runtime thresholds, telemetry, and physical hardware validation remain outstanding.

## 2. Git status

### Committed branch work

The branch contains four commits beyond `raspberry-updated`:

```text
be67ee1d docs: add final DDoS mitigation project plan
17421ffc feat: add multi-layer QEMU camera defense pipeline
e38b67b1 test: add prerecorded QEMU camera streams
166fedd0 test: add portable Raspberry Pi 4 QEMU environment
```

These commits provide the Dockerized QEMU runner, writable SD handling, boot and camera tests, prerecorded camera assets, software-defined WPA2 Wi-Fi environment, XDP/TC security pipeline, blocklists, branch report, and final project-plan PDF.

### Repository state

The implementation and project-plan PDF are committed, and the branch tracks `origin/qemu-raspberry-testing`. Build artifacts under `output-images/` and `buildroot/output/` remain ignored by Git and are local rather than branch-versioned.

## 3. QEMU environment

### 3.1 Raspberry Pi 4 machine

The Docker image uses QEMU 10 and the `raspi4b` machine with:

- ARM Cortex-A72 CPUs
- BCM2711 board model
- Raspberry Pi DTB
- SD/MMC-backed writable storage
- PL011 serial console
- Basic mailbox, GPIO, framebuffer, SPI, I2C, and USB models

The source `output-images/sdcard.img` remains read-only. Normal runs use a persistent Docker volume; unattended tests use temporary writable copies.

### 3.2 Prerecorded camera inputs

Four deterministic Y4M recordings are injected into a writable SD copy:

```text
/dev/vid1 → /opt/qemu-camera/vid1.y4m
/dev/vid2 → /opt/qemu-camera/vid2.y4m
/dev/vid3 → /opt/qemu-camera/vid3.y4m
/dev/vid4 → /opt/qemu-camera/vid4.y4m
```

Each recording is:

- 160×120
- 30 frames/second
- Three seconds long
- A distinct moving color-bar pattern

These paths are regular video files, not V4L2 camera devices.

### 3.3 Loopback camera network

`qemu-camera-network-test.sh` creates four concurrent streams inside the guest:

| Camera | Input | Loopback endpoint |
|---|---|---|
| Camera 1 | `/dev/vid1` | `127.0.0.11:12001` |
| Camera 2 | `/dev/vid2` | `127.0.0.12:12002` |
| Camera 3 | `/dev/vid3` | `127.0.0.13:12003` |
| Camera 4 | `/dev/vid4` | `127.0.0.14:12004` |

FFmpeg encodes and sends all four streams concurrently, and four receivers decode 20 frames each.

### 3.4 Software-defined Wi-Fi environment

The Camera OS now includes:

- `hostapd`
- `wpa_supplicant`
- `iw`
- Built-in `cfg80211`
- Built-in `mac80211`
- Built-in `mac80211_hwsim`

`qemu-wifi-camera-test.sh` creates five software radios:

```text
wlan0  WPA2 access point, 10.30.0.1
wlan1  Camera 1, 10.30.0.11
wlan2  Camera 2, 10.30.0.12
wlan3  Camera 3, 10.30.0.13
wlan4  Camera 4, 10.30.0.14
```

Each station runs in an isolated network namespace and associates with the WPA2 SSID `QEMU-Camera-Lab`. The cameras then transmit video concurrently to AP UDP ports 13001–13004.

The physical Raspberry Pi boot command line sets `mac80211_hwsim.radios=0`, preventing test WLAN devices from appearing during normal hardware deployment.

## 4. Security implementations completed

### 4.1 Genuine TC classifiers

The previous fallback attempted to reuse XDP objects at TC. That was invalid because XDP and TC have different contexts and action values.

Added dedicated programs:

```text
ebpf/tc/common.h
ebpf/tc/cms_filter.bpf.c
ebpf/tc/elastic_filter.bpf.c
```

They use:

- `struct __sk_buff`
- `SEC("classifier")`
- `TC_ACT_OK`
- `TC_ACT_SHOT`
- Direct-action SCHED_CLS attachment

`CONFIG_NET_SCH_INGRESS`, `CONFIG_NET_CLS_BPF`, and `CONFIG_NET_ACT_BPF` are built into the kernel so TC can attach during early boot and under QEMU.

### 4.2 Simultaneous XDP → TC pipeline

Added:

```text
ebpf/xdp/frontend.bpf.c
```

Defended cases now attach both stages:

```text
xdp_cms_ebpf      = XDP validation → TC CMS
xdp_elastic_ebpf  = XDP validation → TC ElasticSketch-inspired filter
```

Correct XDP mode selectors are used:

- `xdpdrv`
- `xdpgeneric`
- `xdpoffload`

If XDP cannot attach, the condition is recorded and TC remains active as an explicit fallback.

### 4.3 Hardened shared parser

Added:

```text
ebpf/include/packet_parser.h
```

XDP and TC now use the same parser, supporting:

- Ethernet bounds validation
- Up to two 802.1Q/802.1ad VLAN tags
- IPv4 version and IHL validation
- IPv4 total-length validation
- Explicit fragment recognition
- UDP length validation
- TCP data-offset validation
- SYN detection
- Source and destination port extraction

More than two VLAN tags are dropped to prevent parser-depth bypasses. Fragmented IPv4 is validated and allowed through XDP, but currently skips TC flow accounting because later fragments do not contain transport headers.

### 4.4 True five-tuple flow keys

Flow keys now contain:

```text
source IPv4
source port
destination IPv4
destination port
protocol
```

The hash includes both ports, and explicit zeroed padding makes BPF map keys deterministic. Existing pinned maps from the older key format must be recreated because their ABI is incompatible.

### 4.5 Removed spoofable APOC bypass

The previous implementation bypassed eBPF whenever source or destination equaled `10.10.0.2`. This allowed source-address spoofing to evade filtering.

The bypass has been removed from all XDP and TC sketch programs. APOC traffic now receives normal flow accounting.

nftables now applies a bounded service policy requiring:

- Expected terminal ingress interface
- APOC source address
- Camera destination subnet
- UDP telemetry destination port
- Maximum 200 packets/second
- Burst of 50 packets

The policy also drops invalid conntrack states and enforces source-prefix anti-spoofing on terminal and camera interfaces.

### 4.6 XDP exact, prefix, and Bloom deny lists

Added:

```text
ebpf/include/blocklist.h
scripts/deployment/blocklistctl.py
```

The Camera OS installs the utility as:

```text
/usr/sbin/airport-blocklist
```

Implemented maps:

| Map | Type | Capacity/purpose |
|---|---|---|
| `exact_deny` | Hash | 4,096 exact IPv4 sources |
| `prefix_deny` | LPM trie | 1,024 IPv4 CIDR prefixes |
| `bloom_bits` | Array | 8,192-bit Bloom filter |
| `defense_cfg` | Array | Bloom enable state |

The Bloom filter uses three hashes and is disabled until explicitly populated or enabled. Exact and LPM lookups are deterministic; Bloom matching may produce false positives.

Maps and the XDP program are pinned under:

```text
/sys/fs/bpf/airport-defense/
```

The attachment script reuses compatible pinned maps across XDP reloads, preserving operational blocklist entries.

## 5. Buildroot integration

The Buildroot package now builds and installs:

```text
/usr/local/sbin/xdp_frontend.bpf.o
/usr/local/sbin/xdp_cms_filter.bpf.o
/usr/local/sbin/xdp_elastic_filter.bpf.o
/usr/local/sbin/tc_cms_filter.bpf.o
/usr/local/sbin/tc_elastic_filter.bpf.o
/usr/sbin/airport-blocklist
```

Relevant kernel capabilities verified in the generated configuration:

```text
CONFIG_BPF=y
CONFIG_BPF_SYSCALL=y
CONFIG_NET_CLS_BPF=y
CONFIG_NET_ACT_BPF=y
CONFIG_NET_SCH_INGRESS=y
CONFIG_CFG80211=y
CONFIG_MAC80211=y
CONFIG_MAC80211_HWSIM=y
```

`BR2_PACKAGE_HOSTAPD=y` is enabled in the Camera OS configuration.

## 6. Current build artifacts

The current local image set was rebuilt after the security changes:

| Artifact | Size | Purpose |
|---|---:|---|
| `output-images/sdcard.img` | 545 MB | Complete bootable SD image |
| `output-images/rootfs.ext2` | 512 MB | Camera OS ext4 root filesystem |
| `output-images/Image` | 26 MB | ARM64 Linux kernel |
| `output-images/boot.vfat` | 32 MB | Raspberry Pi boot filesystem |

Approximate local build storage:

```text
buildroot/output  28 GB
output-images     470 MB allocated on host
```

The SD and root images are sparse, so logical size and allocated host size differ.

## 7. Validation results

### 7.1 Boot and loopback

Passed:

```text
PASS: XDP front-end Ethernet, VLAN, and malformed-frame decisions
PASS: camera1 delivered 20 decoded frames at 127.0.0.11:12001
PASS: camera2 delivered 20 decoded frames at 127.0.0.12:12002
PASS: camera3 delivered 20 decoded frames at 127.0.0.13:12003
PASS: camera4 delivered 20 decoded frames at 127.0.0.14:12004
PASS: OS boot, four video inputs, ffmpeg encode/decode, and four-camera loopback network
```

### 7.2 XDP and TC

All objects compiled and passed the kernel verifier. QEMU confirmed:

```text
PASS: XDP validation front end attached in generic mode
PASS: tc_cms_filter attached as a SCHED_CLS direct-action program
PASS: tc_elastic_filter attached as a SCHED_CLS direct-action program
```

A host namespace test also confirmed a JIT-compiled XDP front end and direct-action TC Elastic classifier attached simultaneously.

### 7.3 Wi-Fi and blocklists

Passed:

```text
PASS: exact IPv4 deny list
PASS: blocklist maps persist across XDP reload
PASS: IPv4 LPM prefix deny list
PASS: Bloom deny list
PASS: camera1 delivered 20 decoded Wi-Fi frames
PASS: camera2 delivered 20 decoded Wi-Fi frames
PASS: camera3 delivered 20 decoded Wi-Fi frames
PASS: camera4 delivered 20 decoded Wi-Fi frames
PASS: WPA2 association, four camera stations, and concurrent video delivery
```

Every camera recovered after its test deny entry was removed or cleared.

### 7.4 APOC trust policy

An 800-packet UDP burst from the legitimate APOC namespace through TC CMS delivered 400 packets, proving that APOC no longer bypasses eBPF.

A 500-packet telemetry burst through nftables accepted the configured burst of 50 and dropped 450.

Normal telemetry regression test:

```text
Sent:    400
Replies: 400
Loss:    0%
```

## 8. Usage

### Build and boot

```bash
./scripts/build/build-docker.sh
./scripts/qemu/build.sh
./scripts/qemu/run.sh
```

### Automated tests

```bash
./scripts/qemu/smoke-test.sh
./scripts/qemu/camera-network-test.sh
./scripts/qemu/wifi-camera-test.sh
```

### Blocklist management

```bash
sudo python3 scripts/deployment/blocklistctl.py exact add 198.51.100.10
sudo python3 scripts/deployment/blocklistctl.py prefix add 203.0.113.0/24
sudo python3 scripts/deployment/blocklistctl.py bloom add 192.0.2.20
sudo python3 scripts/deployment/blocklistctl.py status

sudo python3 scripts/deployment/blocklistctl.py exact delete 198.51.100.10
sudo python3 scripts/deployment/blocklistctl.py prefix delete 203.0.113.0/24
sudo python3 scripts/deployment/blocklistctl.py bloom clear
```

On Camera OS, replace `python3 scripts/deployment/blocklistctl.py` with `airport-blocklist`.

## 9. Important limitations

### QEMU limitations

QEMU `raspi4b` does not emulate:

- BCM2711 GENET Ethernet
- BCM4345 physical Wi-Fi hardware
- CSI camera sensors or Unicam capture
- RF propagation, interference, signal strength, or roaming
- Real NIC driver-mode XDP behavior
- VideoCore hardware encoding performance

The Wi-Fi environment validates Linux mac80211, WPA2, IP, and application behavior, not RF or physical-driver behavior.

### Current software limitations

- Adaptive SYN/SYN-ACK/ACK completion detection is not implemented.
- Fragmented packets currently skip TC flow accounting.
- IPv6 is not parsed or filtered by the eBPF pipeline.
- CMS and Elastic thresholds remain compile-time constants.
- Sketch counters still require multicore synchronization improvements.
- CMS does not yet estimate bytes, connection attempts, or unique ports separately.
- The Elastic implementation is still simplified and single-row on its light side.
- Bloom entries have no individual deletion; clear and rebuild is required.
- Blocklist entries do not yet have expiration timestamps.
- BPF drop-reason telemetry and adaptive userspace control are not implemented.
- APOC policy is bounded but not cryptographically authenticated; a spoofing attacker can consume the allowance.
- Physical Pi camera, Wi-Fi, Ethernet, XDP mode, thermal, and throughput testing remains required.

## 10. Remaining project-plan roadmap

Recommended next implementation order:

1. Adaptive SYN completion-ratio detector.
2. Per-source and per-prefix distributed-flood aggregation.
3. CMS packet, byte, connection-attempt, and port-cardinality estimates.
4. Correct multicore-safe counter updates.
5. Improved ElasticSketch heavy/light promotion and replacement.
6. Runtime configuration maps and adaptive threshold daemon.
7. Kernel drop-reason counters and sampled ring-buffer events.
8. Expanded RTSP, ONVIF, MQTT, telemetry, and logging nftables policy.
9. Exact experiment metrics for XDP/TC drops, CPU, memory, RTT, jitter, and camera FPS.
10. Physical Raspberry Pi 4 and camera-module validation.

## 11. Recommended repository action

The QEMU/Wi-Fi environment, implementations 1–5, report, and project-plan PDF now have checkpoint commits on `qemu-raspberry-testing`. Continue the adaptive SYN detector and subsequent roadmap items as focused incremental commits, rerunning both four-camera QEMU regression tests after every security change.
