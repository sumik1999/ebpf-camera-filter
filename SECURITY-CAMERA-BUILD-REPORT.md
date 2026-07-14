# 📷 Security Camera Embedded Linux Build Report

**Project:** Airport eBPF Defense – Security Camera Edition
**Target Hardware:** Raspberry Pi 4 (ARM64/AArch64)
**Build System:** Buildroot 2026.05
**Date:** 2026-07-14
**Buildroot Version:** 2026.05-394-g9ebfc351be

---

## 🎯 Objective

Build a custom embedded Linux root filesystem for a Raspberry Pi 4 security camera system with:
- **eBPF/XDP packet filters** for real-time network security
- **GStreamer multimedia pipeline** for camera streaming
- **V4L2 camera API** support for hardware cameras
- **Secure remote access** via OpenSSH
- **Web interface** via Lighttpd

---

## 📋 System Requirements

### Build Host (Any OS)
| Requirement | Minimum | Recommended |
|---|---|---|
| OS | Ubuntu 20.04 / macOS 12+ / WSL2 | Ubuntu 22.04 LTS |
| RAM | 8 GB | 16 GB |
| Disk Space | 60 GB | 100 GB |
| CPU | 4 cores | 8+ cores |

### Target Hardware (Raspberry Pi 4)
| Component | Specification |
|---|---|
| SoC | Broadcom BCM2711 (Quad-core Cortex-A72 @ 1.5GHz) |
| Memory | 4GB or 8GB LPDDR4 |
| Storage | 16GB+ microSD card (Class 10/U3) |
| Camera | CSI/MIPI interface (official Pi camera or compatible) |
| Network | Gigabit Ethernet + WiFi (802.11ac) |

---

## 🏗️ Architecture Configuration

| Parameter | Value |
|---|---|
| **Target Architecture** | ARM64 (aarch64) |
| **CPU Model** | Cortex-A72 (ARMv8.2-A) |
| **Endianness** | Little-endian |
| **Kernel Version** | 6.1.0 (Raspberry Pi fork) |
| **C Library** | glibc |
| **Init System** | systemd |

---

## 📦 Package Inventory (252 packages total)

### 🔴 eBPF & Security Filters (Airport Project)

| Package | Purpose | Build Output |
|---|---|---|
| `bpftool` | Load/inspect eBPF programs | `/usr/sbin/bpftool` |
| `libbpf` | eBPF library for C programs | `/usr/lib/libbpf.so` |
| **airport-ebpf** | Custom XDP filter package | See below |
| ↳ `xdp_cms_filter.bpf.o` | Count-Min Sketch rate limiter | `/usr/local/sbin/` |
| ↳ `xdp_elastic_filter.bpf.o` | ElasticSketch (heavy+light) | `/usr/local/sbin/` |

### 📷 Camera & Multimedia

| Package | Version | Purpose |
|---|---|---|
| `gstreamer1` | 1.x | Multimedia framework |
| `gst1-plugins-base` | — | Core plugins (videoconvert, videoscale, audioconvert) |
| `gst1-plugins-good` | — | Good quality plugins (avi, isomp4, wavparse) |
| `gst1-plugins-bad` | — | Experimental/bad quality plugins |
| `ffmpeg` | — | Video encode/decode pipeline |
| `libv4l` | — | Video4Linux2 abstraction layer |
| `libv4l-utils` | — | V4L2 command-line tools |
| `jpeg-turbo` | — | High-performance JPEG codec |
| `libgpiod` | — | GPIO control for camera hardware |
| `libgpiod-tools` | — | GPIO command-line tools |

### 🌐 Networking & Web

| Package | Purpose |
|---|---|
| `lighttpd` + `openssl` | Lightweight HTTPS web server |
| `openssh` (server+client) | Secure remote access |
| `iputils` | ping, arping, clockdiff |
| `bash` | Enhanced shell |
| `wget` | HTTP/FTP downloads |
| `iperf3` | Network bandwidth testing |
| `netcat-openbsd` | Network debugging |

### 📊 Monitoring & Utilities

| Package | Purpose |
|---|---|
| `htop` | Interactive process viewer |
| `sysstat` | Performance monitoring (sar, iostat) |
| `e2fsprogs` | ext2/3/4 filesystem tools |
| `squashfs-tools` | Compressed filesystem support |
| `binutils` | Low-level binary tools |
| `elfutils` | ELF file inspection |

### 🛡️ System

| Package | Purpose |
|---|---|
| `systemd` | System and service manager |
| `busybox` | Essential utilities |
| `zlib` | Compression library |
| `openssl` | Cryptography library |

---

## 🔧 Filesystem Configuration

| Parameter | Value |
|---|---|
| **Type** | ext2/4 |
| **Size** | 120 MB (minimum) / 4096 MB (recommended) |
| **Label** | `rpi-camera` |
| **Block Size** | 4KB |
| **Inode Size** | 256 bytes |

---

## 📂 Kernel Configuration

| Parameter | Value |
|---|---|
| **Version** | 6.1.0 (Raspberry Pi custom) |
| **Defconfig** | `bcm2711` |
| **Device Tree** | `bcm2711-rpi-4-b` (Pi 4 Model B) |
| **DTB Overlays** | Supported |
| **Image Format** | gzip-compressed zImage |
| **Supported DTBs** | rpi-4-b, rpi-400, rpi-cm4, rpi-cm4s |

### Key Kernel Features Enabled
- V4L2 (Video for Linux 2)
- MIPI-CSI2 camera interface
- GPIO character device
- Network stack (TCP/IP, V4L2)
- XDP (eXpress Data Path) support

---

## 🏠 Custom Package: airport-ebpf

### Location
```
buildroot/package/airport-ebpf/
├── Config.in              # Package configuration
└── airport-ebpf.mk        # Build rules
```

### Build Process
1. **Source Location:** `$TOPDIR/..` (project root)
2. **Compiler:** Target cross-compiler (`TARGET_CC`)
3. **Target:** eBPF bytecode (`-target bpf`)
4. **Architecture:** ARM64 (`-D__TARGET_ARCH_arm64`)
5. **Optimization:** `-O2 -g`
6. **Output:** `.bpf.o` ELF files

### Installation Path
```
/usr/local/sbin/
├── xdp_cms_filter.bpf.o     # Count-Min Sketch filter
└── xdp_elastic_filter.bpf.o # ElasticSketch filter
```

### How It Works
```
┌─────────────────────────────────────────────────────────┐
│                    Raspberry Pi 4                       │
│                                                         │
│  [NIC Driver]                                           │
│       │                                                 │
│       ▼ XDP Hook                                        │
│  ┌──────────────┐    ┌──────────────┐                  │
│  │ CMS Filter    │    │ Elastic Filter│                  │
│  │ (Rate Limiter)│    │(Heavy+Light)  │                  │
│  └──────┬───────┘    └──────┬───────┘                  │
│         │                   │                          │
│         ▼                   ▼                          │
│    DROP/ACCEPT          DROP/ACCEPT                    │
│         │                   │                          │
│         ▼                   ▼                          │
│    ┌──────────────────────────────┐                    │
│    │  Linux Network Stack         │                    │
│    │  (GStreamer / Web Server)    │                    │
│    └──────────────────────────────┘                    │
└─────────────────────────────────────────────────────────┘
```

---

## 🚀 Build Instructions (Docker/WSL2)

### Option 1: Docker (Recommended for Windows)

```bash
# 1. Clone the repository
git clone <repository-url>
cd airport-ebpf-mac

# 2. Build the Docker image
docker build -f Dockerfile -t airport-camera-builder .

# 3. Run the build inside the container
docker run --rm -v $(pwd):/build -w /build airport-camera-builder \
    bash -c "cd buildroot && make"

# 4. Retrieve the output
docker run --rm -v $(pwd):/build -w /build -v $(pwd)/output:/output airport-camera-builder \
    bash -c "cp -r /build/output/images /output/"
```

### Option 2: Native Build (WSL2/Ubuntu)

```bash
# 1. Install dependencies
sudo apt update
sudo apt install -y build-essential git bc bison flex libssl-dev \
    make libc6-dev libncurses5-dev wget unzip curl

# 2. Clone and configure
git clone <repository-url>
cd airport-ebpf-mac/buildroot

# 3. Apply configuration
make raspberrypi4_64_defconfig

# 4. Start the build
make

# 5. Output location
ls output/images/
```

### Option 3: macOS (Native)

```bash
# 1. Install dependencies via Homebrew
brew install make ncurses

# 2. Clone and configure
git clone <repository-url>
cd airport-ebpf-mac/buildroot

# 3. Build
make
```

---

## 📤 Deployment to Raspberry Pi 4

### Step 1: Prepare SD Card

```bash
# Find your SD card (BE EXTREMELY CAREFUL)
diskutil list  # macOS
lsblk          # Linux
# or get-item -path env:temp\*  # PowerShell (Windows)

# Unmount (macOS/Linux)
diskutil unmountDisk /dev/diskN  # macOS
umount /dev/sdX*                  # Linux

# Write boot files
sudo dd if=output/images/sdcard/ of=/dev/rdiskN bs=1m  # macOS
sudo dd if=output/images/sdcard/ of=/dev/sdX bs=1m     # Linux
```

### Step 2: Copy Root Filesystem

```bash
# Mount the boot partition
sudo mkdir -p /mnt/boot
sudo mount /dev/sdX1 /mnt/boot  # Linux

# Copy rootfs
cp output/images/rootfs.ext4 /mnt/boot/

# Unmount
sudo umount /mnt/boot
```

### Step 3: Boot & Verify

```bash
# Insert SD card, connect power
# SSH into the device
ssh root@<pi-ip-address>

# Verify packages
which bpftool
which ffmpeg
which gst-launch-1.0
which lighttpd

# Load eBPF filters
bpftool prog load /usr/local/sbin/xdp_cms_filter.bpf.o /sys/fs/bpf/cms
bpftool prog load /usr/local/sbin/xdp_elastic_filter.bpf.o /sys/fs/bpf/elastic
```

---

## 🔍 Troubleshooting

### Build Errors

| Error | Solution |
|---|---|
| `No rule to make target 'menuconfig'` | Use `make <defconfig>` first |
| `legacy configuration detected` | Remove `BR2_LEGACY=y` from `.config` |
| `toolchain headers not detected` | Set `BR2_KERNEL_HEADERS_*` explicitly |
| `uClibc not GLIBC` | Ensure `BR2_TOOLCHAIN_BUILDROOT_GLIBC=y` |
| `BR2_USE_WCHAR not set` | Add `BR2_USE_WCHAR=y` manually |

### Runtime Issues

| Issue | Solution |
|---|---|
| XDP filter won't load | Check `BR2_PACKAGE_BPFTOOL` and `BR2_PACKAGE_LIBBPF` |
| Camera not detected | Verify V4L2 drivers in kernel |
| Network slow | Check XDP mode: `ip link set dev eth0 xdp` |
| High CPU usage | Monitor with `htop` |

---

## 📊 Build Timeline

| Phase | Duration | Notes |
|---|---|---|
| Toolchain download & build | 15–30 min | Single pass, cached after |
| Linux kernel compile | 20–30 min | Parallel builds help |
| User-space packages | 30–60 min | Depends on CPU |
| Root filesystem creation | 5 min | Fast |
| **Total** | **~1–2 hours** | On 8-core machine |

---

## 📁 Final Output Structure

```
output/images/
├── rootfs.ext4              # Complete root filesystem
├── rootfs.ext4.gz           # Compressed (optional)
├── zImage                   # Linux kernel
├── bcm2711-rpi-4-b.dtb      # Device tree
├── bootcode.bin             # Raspberry Pi bootloader
├── start4.elf               # GPU firmware
├── fixup4.dat               # Memory config
├── cmdline.txt              # Kernel command line
├── config.txt               # Firmware config
└── ssh                      # Enable SSH (touch file)
```

---

## 📝 Configuration File Location

The complete Buildroot configuration is saved at:
```
buildroot/.config
```

To replicate this exact configuration:
```bash
cd buildroot
cp .config .config.backup  # Save current
make olddefconfig           # Apply configuration
```

---

## 🔄 Replication Steps for Windows (WSL2)

1. **Install WSL2** with Ubuntu 22.04
2. **Clone the repository**
3. **Run the Docker build** (recommended) or use native build
4. **Copy output images** to your host machine
5. **Flash to SD card** using Win32DiskImager (Windows) or `dd` (WSL2)

---

**Last Updated:** 2026-07-14
**Repository:** airport-ebpf-mac
**Buildroot Commit:** 2026.05-394-g9ebfc351be
