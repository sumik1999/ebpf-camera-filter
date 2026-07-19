# eBPF Security Camera – Raspberry Pi 4

**Embedded Linux + eBPF XDP network defense** for Raspberry Pi 4 camera systems.

This project builds a **ready-to-flash Raspberry Pi 4 SD card image** with:
- Custom Linux kernel with eBPF/XDP support
- Pre-compiled XDP eBPF security filters (Count-Min Sketch, ElasticSketch)
- Camera streaming pipeline (GStreamer, V4L2)
- Web interface for monitoring
- Network traffic simulation tools

---

## 📦 What You Get

```
output-images/
├── sdcard.img          # Full SD card (boot VFAT + rootfs ext4)
├── rootfs.ext4         # Standalone root filesystem (120MB)
├── boot.vfat           # Standalone boot partition (32MB)
├── Image               # Linux kernel ARM64 (24MB)
├── bcm2711-rpi-*.dtb   # Device tree blobs for Pi 4 variants
├── rpi-firmware/
│   ├── start4.elf      # GPU bootloader
│   ├── fixup4.dat      # Memory config
│   ├── config.txt      # Boot configuration
│   └── overlays/       # Device tree overlays (camera, etc.)
└── genimage.cfg        # Partition layout definition
```

---

## 🏗️ Build the SD Card Image

The build uses **Buildroot** (embedded Linux build system) inside Docker. This works on **any OS** — Linux, macOS, or Windows.

### Prerequisites

| Requirement | Minimum | Recommended |
|---|---|---|
| **Docker Desktop** | v20.10+ | v24+ |
| **Disk Space** | 60 GB free | 100 GB+ |
| **RAM** | 8 GB | 16 GB+ |
| **CPU** | 4 cores | 8+ cores |

> **No compilers, SDKs, or cross-toolchains needed** — Docker handles everything.

---

### Quick Build (Recommended)

#### macOS

```bash
# 1. Clone the repository
git clone <your-repo-url>
cd ebpf-camera-filter

# 2. Run the build script
chmod +x build-docker.sh
./build-docker.sh

# 3. Wait 1–2 hours...
# ✅ Output will be in: output-images/
```

#### Windows (PowerShell)

```powershell
# 1. Clone the repository
git clone <your-repo-url>
cd ebpf-camera-filter

# 2. Run the build script
.\build.ps1

# 3. Wait 1–2 hours...
# ✅ Output will be in: output-images/
```

#### Linux

```bash
git clone <your-repo-url>
cd ebpf-camera-filter
chmod +x build-docker.sh
./build-docker.sh
```

---

### Manual Build (Any OS)

If the scripts don't work, build directly with Docker:

```bash
# 1. Build the Docker image
docker build -t airport-camera-builder -f Dockerfile .

# 2. Run the build (mounts current directory as /build)
docker run --rm -v "$(pwd)":/build -w /build airport-camera-builder

# 3. Copy output from container
docker run --rm -v "$(pwd)":/build -w /build airport-camera-builder \
    bash -c "cp -r /build/buildroot/output/images/* /build/output-images/ 2>/dev/null || true"
```

---

### Build Troubleshooting

| Problem | Solution |
|---|---|
| **Docker DNS resolution fails** | Run `./fix-docker-dns.sh` and restart Docker Desktop |
| **Out of disk space** | Free 60+ GB; Docker Desktop needs room for the build cache |
| **Build takes forever** | Increase Docker Desktop resources (CPU cores, RAM) |
| **Package download fails** | Run `./fix-docker-dns.sh`, it fixes Ubuntu mirror access |
| **Permission denied** | Ensure you're in the project root when running the script |

**DNS Fix (macOS/Windows):**
```bash
./fix-docker-dns.sh
# 1. Completely quit Docker Desktop
# 2. Wait 5 seconds
# 3. Reopen Docker Desktop
# 4. Try the build again
```

---

### Build Options

Edit `config.env` before building:

```bash
# Kernel version
BR2_LINUX_KERNEL_CUSTOM_VERSION_VALUE="6.6.67"

# Root filesystem size (MB)
BR2_ROOTFS_SIZE="120"

# Packages to include
BR2_PACKAGE_GST1_PLUGINS_BAD=y
BR2_PACKAGE_GST1_PLUGINS_PLUGINS_VIDEOPARSERS=y
BR2_PACKAGE_LIBBPF=y
```

---

## 💾 Flash to SD Card

### Option A: Use the pre-built `sdcard.img`

This is a single file with both boot + root partitions.

#### macOS

```bash
# 1. Find the SD card (IMPORTANT: do NOT pick your hard drive!)
diskutil list

# Look for your SD card, e.g., /dev/disk2

# 2. Unmount (not eject)
diskutil unmountDisk /dev/diskN

# 3. Write image (use rdiskN for 5x faster writes)
sudo dd if=output-images/sdcard.img of=/dev/rdiskN bs=1m conv=sync

# 4. Eject
diskutil eject /dev/diskN
```

#### Windows

1. Download [Win32DiskImager](https://sourceforge.net/projects/win32diskimager/)
2. Extract and run as Administrator
3. **Image file:** Browse to `output-images/sdcard.img`
4. **Device:** Select your SD card (⚠️ verify size!)
5. Click **Write** (this will erase the SD card)
6. Wait 5–10 minutes for completion

#### Linux

```bash
# Find the SD card
lsblk

# Write the image (replace sdx with your device)
sudo dd if=output-images/sdcard.img of=/dev/sdX bs=4M status=progress conv=sync

# Sync and eject
sync
sudo eject /dev/sdX
```

---

### Option B: Flash partitions separately

```bash
# Flash boot partition (VFAT)
sudo dd if=output-images/boot.vfat of=/dev/rdiskN bs=1m conv=sync

# Copy root filesystem to the boot partition
cp output-images/rootfs.ext4 /Volumes/boot/
```

---

## 🔌 Deploy to Raspberry Pi

### Hardware Setup

```
┌──────────────────────────────────────────┐
│         Raspberry Pi 4 Model B           │
│                                          │
│  USB-C ───▶  Power (5V/3A)              │
│  Ethernet ─▶  Network / Internet         │
│  CSI/USB  ───▶ Camera Module              │
│  HDMI   ───▶  Monitor (optional)         │
└──────────────────────────────────────────┘
```

### SD Card → Pi → Power On

1. Insert SD card into Raspberry Pi 4
2. Connect Ethernet cable
3. Connect camera (CSI ribbon or USB)
4. Plug in power supply
5. Wait 2–3 minutes for first boot (rootfs may expand)

### Find the Pi on the Network

```bash
# macOS / Linux
arp -a | grep b8:27:eb
nmap -sn 192.168.1.0/24

# Windows
arp -a | findstr b8-27-eb
```

### SSH Into the Pi

```bash
ssh root@<pi-ip-address>
# Default: No password required (set on first login)
```

---

## 📷 Verify the Installation

```bash
# Kernel
uname -r          # Should show custom kernel with eBPF
uname -a

# eBPF tools
which bpftool
which libbpf

# Camera stack
v4l2-ctl --list-devices
gst-launch-1.0 --version

# Network
ip addr show
ping 8.8.8.8
```

---

## 🛡️ Load eBPF Security Filters

```bash
# List available filters
ls -la /usr/local/sbin/*.bpf.o

# Load Count-Min Sketch filter
bpftool prog load /usr/local/sbin/xdp_cms_filter.bpf.o /sys/fs/bpf/cms

# Load ElasticSketch filter
bpftool prog load /usr/local/sbin/xdp_elastic_filter.bpf.o /sys/fs/bpf/elastic

# Verify
bpftool prog list

# Attach to interface (eth0 for Ethernet)
ip link set dev eth0 xdp obj /usr/local/sbin/xdp_cms_filter.bpf.o sec xdp
```

---

## 🧪 Run eBPF Experiments (Test Harness)

To run the attack/defense simulation on your **build host** (Linux VM, WSL2, or native Linux):

```bash
# 1. Check environment (requires clang, iproute2, python3)
sudo make check

# 2. Run full attack matrix
sudo make stress-matrix

# 3. Run visual demo
sudo make visual-demo

# 4. View results
open eval/results/visual_replay.html
```

### Defense Cases

| Case | Hook | Description |
|------|------|-------------|
| `no_defense` | — | Baseline, no filtering |
| `linux_filter` | nftables | Conventional firewall |
| `xdp_cms_ebpf` | XDP | Count-Min Sketch rate limiter |
| `xdp_elastic_ebpf` | XDP | ElasticSketch (heavy+light) |

### Run a Single Case

```bash
sudo CASE=xdp_elastic_ebpf ATTACK_PPS=3000 bash scripts/run_experiment.sh
```

---

## 📁 Repository Layout

```
ebpf-camera-filter/
├── Dockerfile                  # Docker build image (Buildroot + toolchain)
├── build-docker.sh             # macOS/Linux build script
├── build.ps1                   # Windows build script
├── build-native.sh             # macOS native build fallback
├── fix-docker-dns.sh           # Fix Docker DNS on macOS
├── Makefile                    # eBPF test harness rules
├── config.env                  # Experiment configuration
├── output-images/              # Build output (SD card images)
├── buildroot/                  # Buildroot embedded Linux config
│   ├── output/                 # Build artifacts (gitignored)
│   └── package/                # Custom packages
├── ebpf/                       # eBPF source code
│   ├── common.h                # Packet parser (Berkeley BPF heritage)
│   ├── maps.h                  # Shared map/struct definitions
│   ├── xdp_cms_filter.bpf.c    # Count-Min Sketch filter
│   └── xdp_elastic_filter.bpf.c # ElasticSketch filter
├── scripts/                    # Experiment scripts
│   ├── check_env.sh            # Pre-flight checks
│   ├── setup_ns_tree.sh        # Network namespace topology
│   ├── attach_xdp.sh           # XDP attach (auto arch detect)
│   ├── attach_tc_fallback.sh   # TC ingress fallback
│   ├── detach_xdp.sh           # Detach XDP
│   ├── cleanup_ns.sh           # Tear down topology
│   ├── run_experiment.sh       # Single experiment
│   ├── run_stress_matrix.sh    # Full attack matrix
│   └── run_visual_demo.sh      # Visual demo
├── filters/                    # nftables defense rules
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
├── setup-guide.md              # Flashing & deployment guide
├── build-readme.md             # Build troubleshooting
├── security-camera-build-report.md
├── qemu-boot.sh                # QEMU VM boot script
└── qemu-testing/               # QEMU-based testing
```

---

## 🔧 Customization

### Change Kernel Version

```bash
# Edit buildroot/package/linux-custom/linux-custom.hash
# Edit buildroot/Config.in to set BR2_LINUX_KERNEL_CUSTOM_VERSION_VALUE
```

### Add/Remove Packages

```bash
# Enable/disable packages in Buildroot menuconfig
cd buildroot
make menuconfig
# Navigate to: Target Packages → ...
make
```

### Configure Boot

```bash
# Edit boot partition configuration
output-images/rpi-firmware/config.txt
output-images/rpi-firmware/cmdline.txt
```

---

## 📊 System Monitoring (On Pi)

```bash
# Real-time monitoring
htop
ip -s link show eth0
df -h

# eBPF stats
bpftool prog show
dmesg | grep -i xdp
dmesg | grep -i bpf

# Camera pipeline
v4l2-ctl --list-devices
gst-inspect-1.0 | grep v4l2
```

---

## 🔄 Update After Initial Build

### Rebuild Only eBPF Filters

```bash
cd ebpf/
make xdp_cms_filter.bpf.o xdp_elastic_filter.bpf.o
# Copy to Pi
scp *.bpf.o root@<pi-ip>:/usr/local/sbin/
```

### Full Image Rebuild

```bash
cd buildroot
make clean
make
```

---

## 📚 Additional Resources

- **Buildroot Manual:** https://buildroot.org/downloads/manual/manual.html
- **Raspberry Pi Docs:** https://www.raspberrypi.com/documentation/
- **eBPF/XDP Reference:** https://docs.kernel.org/bpf/prog_xdp.html
- **SETUP-GUIDE.md:** Detailed flashing & deployment steps
- **BUILD-README.md:** Build troubleshooting details

---

**Last Updated:** 2025-07-19  
**Version:** 1.0  
**Target:** Raspberry Pi 4 (ARM64)
