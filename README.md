# eBPF Security Camera

**eBPF XDP network defense** — Buildroot-compiled embedded Linux for Raspberry Pi 4 with Count-Min Sketch and ElasticSketch rate limiters.

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
![Buildroot](https://img.shields.io/badge/Buildroot-2025.02-green)
![eBPF](https://img.shields.io/badge/eBPF-XDP-orange)

---

## Overview

This project builds a complete embedded Linux image (via [Buildroot](https://buildroot.org/)) targeting **Raspberry Pi 4 (ARM64)** and provides an **eBPF test harness** for evaluating XDP-based network security filters.

**Two deliverables:**

| Artifact | Purpose |
|----------|---------|
| **SD card image** | Flashable Raspberry Pi image with kernel, eBPF tools, and camera pipeline |
| **Test harness** | Attack/defense simulation on any Linux host (native, WSL2, Docker VM) |

---

## Quick Start

### Build the SD Card Image

Docker-based build works on **Linux, macOS, and Windows**. No compilers required.

```bash
# macOS / Linux
chmod +x scripts/build/build-docker.sh && ./scripts/build/build-docker.sh

# Windows (PowerShell)
.\scripts\build\build.ps1

# Manual
docker build -t camera-builder -f containers/Dockerfile .
docker run --rm -v "$(pwd)":/build -w /build camera-builder
```

**Output:** `output-images/` contains `sdcard.img`, `rootfs.ext4`, kernel, and device trees.

### Boot the Image in Raspberry Pi 4 QEMU

Docker packages QEMU 10 and the required utilities, so host QEMU packages are
not needed:

```bash
./scripts/qemu/build.sh
./scripts/qemu/run.sh

# Unattended OS, eBPF, video, and four-camera loopback validation
./scripts/qemu/camera-network-test.sh

# WPA2 AP plus four isolated software Wi-Fi camera stations
./scripts/qemu/wifi-camera-test.sh
```

Log in as `root` with an empty password. The environment places four
prerecorded test streams at `/dev/vid1` through `/dev/vid4`. See
[`qemu/README.md`](qemu/README.md) for usage, persistent-disk handling, and
emulation limitations. QEMU's Pi 4 model does not emulate GENET Ethernet or
CSI cameras, so these streams are regular files rather than V4L2 devices.

### Run the eBPF Test Harness

```bash
# Pre-flight checks
sudo make check

# Full attack/defense matrix
sudo make stress-matrix

# Visual demo with attack simulation
sudo make visual-demo
```

---

## Build System

### SD Card Image (Buildroot)

The Buildroot configuration produces a camera-focused embedded filesystem with:

- Custom Linux kernel (camera, Wi-Fi, nftables, eBPF/XDP enabled)
- eBPF tools (`bpftool`, `libbpf`) and both project XDP filter objects
- libcamera/rpicam applications and GStreamer camera/RTSP pipeline
- Raspberry Pi 4 Wi-Fi firmware, `wpa_supplicant`, and `dhcpcd`
- nftables firewall and the lightweight `opkg` package manager

```
output-images/
├── sdcard.img          # Full flashable image (boot + rootfs)
├── rootfs.ext4         # Standalone root filesystem
├── boot.vfat           # Standalone boot partition
├── Image               # ARM64 Linux kernel
├── bcm2711-rpi-*.dtb   # Device tree blobs
└── rpi-firmware/       # Bootloader & overlays
```

The writable ext4 root filesystem is provisioned at 512 MiB. The installed
files currently consume about 180 MiB, leaving roughly 300 MiB for runtime
state and controlled updates. Buildroot images are sparse files: compare
logical and host-allocated sizes with `du -h --apparent-size` and `du -h`.

To audit which packages consume the target filesystem, the builder includes
the optional dependencies required by Buildroot's size report:

```bash
docker run --rm -v "$PWD:/build" -w /build airport-camera-builder \
  bash -c 'cd buildroot && make graph-size'
# Reports: buildroot/output/graphs/package-size-stats.csv and graph-size.pdf
```

**Build requirements:**

| Requirement | Minimum | Recommended |
|-------------|---------|-------------|
| Docker Desktop | v20.10 | v24+ |
| Disk Space | 60 GB | 100 GB+ |
| RAM | 8 GB | 16 GB+ |
| CPU | 4 cores | 8+ cores |

### Test Harness

The harness simulates multi-camera networks under attack using Linux network namespaces and bridges.

```bash
# Defense cases
make stress-matrix    # Full attack matrix
make visual-demo      # Single-case visualization
```

| Case | Hook | Defense |
|------|------|---------|
| `no_defense` | — | Baseline |
| `linux_filter` | nftables | Standard firewall |
| `xdp_cms_ebpf` | XDP → TC | XDP validation + TC Count-Min Sketch |
| `xdp_elastic_ebpf` | XDP → TC | XDP validation + TC ElasticSketch |

---

## Architecture

```
┌──────────────────────────────────────────────┐
│              Build Host                      │
│  ┌────────────────┐    ┌──────────────────┐ │
│  │  buildroot/    │    │  Test Harness    │ │
│  │  Docker build  │    │  (make stress)   │ │
│  └────────┬───────┘    └────────┬─────────┘ │
│           │                     │            │
│  output-images/          scripts/              │
│  ┌──────────────────┐    ┌─────────────────┐ │
│  │ sdcard.img       │    │ ns topology     │ │
│  │ kernel + rootfs  │    │ attack/defense  │ │
│  └──────────────────┘    └─────────────────┘ │
└──────────────────────────────────────────────┘
```

### eBPF Hooks

| Hook | Speed | Use Case |
|------|-------|----------|
| **XDP** | Fastest (pre-skb) | Early header validation and drops |
| **TC ingress** | Fast (post-skb) | Stateful CMS/Elastic flow analytics |

Defended cases attach both stages in order. If the interface cannot support XDP,
the harness explicitly reports that condition and continues with a TC-only fallback.

### Packet Flow

```
xdp_md { data, data_end }
   ↓
struct ethhdr → optional 802.1Q/802.1ad VLAN tags
   ↓
struct iphdr → version, IHL, total length and fragment validation
   ↓
struct udphdr / tcphdr → source/destination ports, lengths, SYN detection
   ↓
BPF maps (Count-Min Sketch / ElasticSketch)
```

All pointers are bounds-checked against `ctx->data_end`. The eBPF verifier enforces these checks at load time.

### XDP blocklist management

The attachment script pins XDP maps under `/sys/fs/bpf/airport-defense`. Manage
exact addresses, CIDR prefixes, and the optional Bloom filter with:

```bash
sudo python3 scripts/deployment/blocklistctl.py exact add 198.51.100.10
sudo python3 scripts/deployment/blocklistctl.py prefix add 203.0.113.0/24
sudo python3 scripts/deployment/blocklistctl.py bloom add 192.0.2.20
sudo python3 scripts/deployment/blocklistctl.py status

sudo python3 scripts/deployment/blocklistctl.py exact delete 198.51.100.10
sudo python3 scripts/deployment/blocklistctl.py prefix delete 203.0.113.0/24
sudo python3 scripts/deployment/blocklistctl.py bloom clear
```

Camera OS installs the same utility as `airport-blocklist`. Exact and LPM maps
do not produce false positives. Bloom filtering is disabled until explicitly
populated/enabled and can produce false positives; individual Bloom entries
cannot be deleted, so rebuild it with `bloom clear` when entries expire.

---

## Repository Layout

```
ebpf-camera-filter/
├── README.md
├── Makefile
├── config/                     # Public defaults and local override example
├── containers/                 # Build and QEMU Compose definitions
├── docs/
│   ├── guides/                 # Build and setup guides
│   ├── reports/                # Engineering reports in Markdown/PDF
│   └── references/             # Project plans and research PDFs
├── ebpf/
│   ├── include/                # Shared maps and packet parser
│   ├── xdp/                    # XDP front end and standalone prototypes
│   └── tc/                     # TC CMS and Elastic classifiers
├── policy/nftables/            # Apply and clear nftables policy
├── scripts/
│   ├── build/                  # Native, Docker, and Windows builds
│   ├── deployment/             # XDP/TC attach and blocklist control
│   ├── experiments/            # Experiment and stress runners
│   ├── network/                # Namespace topology lifecycle
│   ├── qemu/                   # QEMU build, boot, and regression tests
│   └── validation/             # Environment checks
├── qemu/
│   ├── assets/                 # Four prerecorded streams
│   └── guest/                  # Guest init and test scripts
├── traffic/                    # Traffic generators
├── eval/                       # Metrics and visualization
├── buildroot/                  # Vendored Buildroot and project integration
└── output-images/              # Generated images (ignored)
```

---

## Configuration

### Buildroot

```bash
cd buildroot
make clean                       # Required when migrating from the old uClibc image
make rpi4_camera_ebpf_defconfig  # Apply the canonical camera OS configuration
make menuconfig                  # Optional interactive customization
make                             # Build the image
```

Provision Wi-Fi on the running device without baking credentials into the image:

```bash
wifi-setup US "your-ssid" "your-passphrase"
wpa_cli status
```

`opkg` is intended for local, release-matched field updates. Buildroot does not
provide a binary feed; configure your own feed before running `opkg update`.

Key config files:

| File | Purpose |
|------|---------|
| `buildroot/configs/rpi4_camera_ebpf_defconfig` | Canonical OS package selection |
| `buildroot/.config` | Generated Buildroot configuration |
| `buildroot/board/rpi4-camera/linux-camera-ebpf.fragment` | Kernel camera/Wi-Fi/eBPF/nftables options |
| `output-images/rpi-firmware/config.txt` | Raspberry Pi boot config |
| `output-images/rpi-firmware/cmdline.txt` | Kernel command line |

### Test Harness

Safe defaults are tracked in `config/defaults.env`. For local overrides, copy
`config/local.env.example` to the ignored `config/local.env` and edit that file:

```bash
NUM_CAMERAS=4           # Simulated cameras
TEST_DURATION_SEC=30    # Experiment length
ATTACH_MODE=xdp         # XDP or TC
CMS_WIDTH=1024          # Sketch dimensions
HEAVY_FLOW_THRESHOLD_PPS=400  # Heavy hitter threshold
```

---

## Build Troubleshooting

| Problem | Solution |
|---------|----------|
| Docker DNS fails | Run `./scripts/build/fix-docker-dns.sh` and restart Docker Desktop |
| Out of disk | Free 60+ GB; Docker needs space for build cache |
| Slow build | Increase Docker resources (CPU cores, RAM) |
| Package download fails | `./scripts/build/fix-docker-dns.sh` fixes mirror access |
| Permission denied | Ensure you're in the project root |

---

## Documentation

| File | Description |
|------|-------------|
| [SETUP-GUIDE.md](docs/guides/SETUP-GUIDE.md) | Flashing and deployment guide |
| [BUILD-README.md](docs/guides/BUILD-README.md) | Build troubleshooting details |
| [docs/reports/SECURITY-CAMERA-BUILD-REPORT.md](docs/reports/SECURITY-CAMERA-BUILD-REPORT.md) | Technical report |
| [QEMU branch report](docs/reports/QEMU-RASPBERRY-TESTING-BRANCH-REPORT.md) | QEMU, Wi-Fi, and eBPF status |
| [Next-steps handoff](docs/reports/next_steps.pdf) | Prioritized remaining work and repository manipulation map |
| [DDoS project plan](docs/references/DDoS_Mitigation_Project_Plan_FINAL.pdf) | Final mitigation architecture plan |

---

**Version:** 1.0  
**Target:** Raspberry Pi 4 (ARM64)
