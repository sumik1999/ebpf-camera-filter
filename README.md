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
chmod +x build-docker.sh && ./build-docker.sh

# Windows (PowerShell)
.\build.ps1

# Manual
docker build -t camera-builder -f Dockerfile .
docker run --rm -v "$(pwd)":/build -w /build camera-builder
```

**Output:** `output-images/` contains `sdcard.img`, `rootfs.ext4`, kernel, and device trees.

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
| `xdp_cms_ebpf` | XDP | Count-Min Sketch |
| `xdp_elastic_ebpf` | XDP | ElasticSketch |

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
| **XDP** | Fastest (pre-skb) | High-throughput rate limiting |
| **TC ingress** | Fast (post-skb) | Fallback for virtual interfaces |

The test harness auto-selects XDP mode for real NICs and falls back to TC ingress for virtual Ethernet (Docker VMs, Lima, OrbStack).

### Packet Flow

```
xdp_md { data, data_end }
   ↓
struct ethhdr → ETH_P_IP check
   ↓
struct iphdr → IHL validation, proto extraction
   ↓
struct udphdr / tcphdr → port, SYN detection
   ↓
BPF maps (Count-Min Sketch / ElasticSketch)
```

All pointers are bounds-checked against `ctx->data_end`. The eBPF verifier enforces these checks at load time.

---

## Repository Layout

```
ebpf-camera-filter/
├── Dockerfile                  # Buildroot build image
├── build-docker.sh             # macOS/Linux build script
├── build.ps1                   # Windows build script
├── build-native.sh             # macOS native fallback
├── fix-docker-dns.sh           # Docker DNS fix (macOS)
├── Makefile                    # Test harness targets
├── config.env                  # Experiment config
├── output-images/              # Build artifacts (SD card images)
├── buildroot/                  # Buildroot configuration
│   ├── output/                 # Build artifacts (gitignored)
│   └── package/                # Custom packages
├── ebpf/                       # eBPF source code
│   ├── common.h                # Packet parser
│   ├── maps.h                  # Shared maps/structures
│   ├── xdp_cms_filter.bpf.c    # Count-Min Sketch filter
│   └── xdp_elastic_filter.bpf.c # ElasticSketch filter
├── scripts/                    # Test harness scripts
│   ├── check_env.sh            # Pre-flight checks
│   ├── setup_ns_tree.sh        # Network namespace topology
│   ├── attach_xdp.sh           # XDP attach
│   ├── attach_tc_fallback.sh   # TC fallback
│   ├── detach_xdp.sh           # Detach filters
│   ├── cleanup_ns.sh           # Cleanup topology
│   ├── run_experiment.sh       # Single experiment
│   ├── run_stress_matrix.sh    # Full matrix
│   └── run_visual_demo.sh      # Visualization
├── filters/                    # nftables rules
│   ├── nftables_guard.sh
│   └── clear_nftables.sh
├── traffic/                    # Traffic generators
│   ├── camera_service.py
│   ├── syn_flood.py
│   ├── telemetry_client.py
│   ├── telemetry_server.py
│   └── udp_flood.py
├── eval/                       # Metrics & visualization
│   ├── collect_metrics.py
│   ├── summarize_results.py
│   └── visual_attack_sim.py
├── docs/
├── setup-guide.md
├── build-readme.md
└── security-camera-build-report.md
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

Edit `config.env` before running:

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
| Docker DNS fails | Run `./fix-docker-dns.sh` and restart Docker Desktop |
| Out of disk | Free 60+ GB; Docker needs space for build cache |
| Slow build | Increase Docker resources (CPU cores, RAM) |
| Package download fails | `./fix-docker-dns.sh` fixes Ubuntu mirror access |
| Permission denied | Ensure you're in the project root |

---

## Documentation

| File | Description |
|------|-------------|
| [setup-guide.md](setup-guide.md) | Flashing & deployment guide |
| [build-readme.md](build-readme.md) | Build troubleshooting details |
| [security-camera-build-report.md](security-camera-build-report.md) | Technical report |

---

**Version:** 1.0  
**Target:** Raspberry Pi 4 (ARM64)
