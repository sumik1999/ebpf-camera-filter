# 🚀 Setup Guide: Airport eBPF Security Camera

Complete guide for building and deploying the embedded Linux security camera image on **macOS** and **Windows**.

---

## 📋 Table of Contents

- [Prerequisites](#prerequisites)
- [Build the Image](#build-the-image)
- [Flash to SD Card](#flash-to-sd-card)
- [Deploy to Raspberry Pi](#deploy-to-raspberry-pi)
- [Load eBPF Filters](#load-ebpf-filters)
- [Test Camera Streaming](#test-camera-streaming)
- [Troubleshooting](#troubleshooting)

---

## 📦 Prerequisites

### Hardware

| Item | Requirement |
|---|---|
| **Raspberry Pi 4** | Model B, 4GB or 8GB RAM |
| **SD Card** | 16GB+ (Class 10/U3 recommended) |
| **Camera** | Raspberry Pi Camera Module (CSI) or compatible USB camera |
| **Power Supply** | USB-C 5V/3A (official Pi 4 supply recommended) |
| **Ethernet Cable** | For network connectivity |

### Build Host (Mac or Windows)

| Software | Minimum | Recommended |
|---|---|---|
| **Docker Desktop** | v20.10+ | v24+ |
| **Disk Space** | 60 GB free | 100 GB+ |
| **RAM** | 8 GB | 16 GB+ |

> **Note:** Docker is required for building. No additional compilers or tools needed!

---

## 🏗️ Build the Image

### macOS

```bash
# 1. Clone the repository (if not already done)
git clone <your-repo-url>
cd airport-ebpf-mac

# 2. Build using Docker
chmod +x build-docker.sh
./build-docker.sh

# Wait 1-2 hours for the build to complete...
# ✅ Output files will be in: output-images/
```

### Windows

```powershell
# 1. Clone the repository (if not already done)
git clone <your-repo-url>
cd airport-ebpf-mac

# 2. Build using PowerShell
.\build.ps1

# Wait 1-2 hours for the build to complete...
# ✅ Output files will be in: output-images/
```

### Manual Build (Any OS)

```bash
# Build directly with Docker
docker build -t airport-camera-builder -f Dockerfile .
docker run --rm airport-camera-builder

# Output will be in buildroot/output/images/
```

### Build Output

After building, you should see these files in `output-images/`:

```
output-images/
├── rootfs.ext4              # Main root filesystem
├── zImage                   # Linux kernel
├── bcm2711-rpi-4-b.dtb      # Device tree (Pi 4)
├── bootcode.bin             # Bootloader
├── start4.elf               # GPU firmware
├── fixup4.dat               # Memory config
└── cmdline.txt              # Kernel parameters
```

---

## 💾 Flash to SD Card

### macOS

```bash
# 1. Insert SD card
# 2. Find the SD card device (IMPORTANT: don't pick your hard drive!)
diskutil list

# Look for something like /dev/disk2 (usually 14-30GB)

# 3. Unmount the SD card
diskutil unmountDisk /dev/diskN   # Replace N with your disk number

# 4. Flash the boot partition
sudo dd if=output-images/sdcard/ of=/dev/rdiskN bs=1m
# ⚠️ Use /dev/rdiskN (raw) for faster writes

# 5. Copy rootfs
cp output-images/rootfs.ext4 /Volumes/boot/

# 6. Eject
diskutil eject /dev/diskN
```

### Windows

1. **Download Win32DiskImager:** https://sourceforge.net/projects/win32diskimager/
2. **Extract and run as Administrator**
3. **Select Image:** Browse to `output-images/rootfs.ext4`
4. **Select Device:** Choose your SD card (⚠️ verify size!)
5. **Click "Write"** (this will erase the SD card!)
6. **Wait for completion** (~5-10 minutes)
7. **Eject SD card safely**

> **Alternative (Windows with WSL2):**
> ```bash
> # From WSL2 terminal
> sudo apt install gparted
> sudo gparted  # Select SD card, flash image
> ```

---

## 🔌 Deploy to Raspberry Pi

### 1. Insert SD Card

```
SD Card → Raspberry Pi 4
```

### 2. Connect Hardware

```
┌─────────────────────────────────────┐
│         Raspberry Pi 4              │
│                                     │
│  ┌───────────┐    ┌────────────┐   │
│  │ USB-C     │───▶│ Power      │   │
│  │ Ethernet  │───▶│ Internet   │   │
│  │ CSI/USB   │───▶│ Camera     │   │
│  └───────────┘    └────────────┘   │
└─────────────────────────────────────┘
```

### 3. Power On

- Plug in power supply
- Wait 2-3 minutes for boot
- LED should show steady green (activity)

### 4. Find Pi IP Address

```bash
# macOS
arp -a | grep b8:27:eb    # Pi MAC prefix

# Windows
arp -a | findstr b8-27-eb

# Or use nmap
nmap -sn 192.168.1.0/24
```

---

## 🔧 Load eBPF Filters

### SSH into the Pi

```bash
ssh root@<pi-ip-address>
# Default password: (empty, set on first login)
```

### Verify Installation

```bash
# Check eBPF tools
which bpftool
which libbpf

# Check camera tools
which gst-launch-1.0
which ffmpeg

# Check web server
which lighttpd

# List eBPF filter files
ls -la /usr/local/sbin/*.bpf.o
```

### Load Filters

```bash
# Load Count-Min Sketch filter
bpftool prog load /usr/local/sbin/xdp_cms_filter.bpf.o /sys/fs/bpf/cms

# Load ElasticSketch filter
bpftool prog load /usr/local/sbin/xdp_elastic_filter.bpf.o /sys/fs/bpf/elastic

# Verify loaded
bpftool prog list
```

### Attach to Network Interface

```bash
# Get interface name
ip link show

# Attach filter to eth0 (or wlan0)
ip link set dev eth0 xdp obj /usr/local/sbin/xdp_cms_filter.bpf.o sec xdp

# Or use native mode (if NIC supports it)
ip link set dev eth0 xdp obj /usr/local/sbin/xdp_cms_filter.bpf.o sec xdp mode native
```

---

## 📷 Test Camera Streaming

### GStreamer Pipeline

```bash
# List available cameras
v4l2-ctl --list-devices

# Test with CSI camera (replace with your device)
gst-launch-1.0 v4l2src device=/dev/video0 ! \
    videoconvert ! autovideosink

# Stream to network (RTSP)
gst-launch-1.0 v4l2src device=/dev/video0 ! \
    videoconvert ! x264enc ! rtph264pay ! \
    udpsink host=192.168.1.100 port=5000

# View stream on another machine
gst-launch-1.0 udpsrc port=5000 ! \
    rtph264depay ! h264parse ! avdec_h264 ! \
    autovideosink
```

### FFmpeg Streaming

```bash
# Capture and stream
ffmpeg -f v4l2 -i /dev/video0 -f mpegts udp://192.168.1.100:5000

# View with VLC
vlc udp://@:5000
```

### Web Interface

```bash
# Start lighttpd
lighttpd -f /etc/lighttpd/lighttpd.conf

# Access from browser
# http://<pi-ip-address>
```

---

## 🔍 Troubleshooting

### Build Issues

| Problem | Solution |
|---|---|
| **Docker won't start** | Check Docker Desktop is running |
| **Build fails on `make olddefconfig`** | Run `make clean` and retry |
| **Out of disk space** | Free up 60+ GB, Docker needs space |
| **Slow build** | Check `nproc` output, increase Docker resources |

### Runtime Issues

| Problem | Solution |
|---|---|
| **Can't SSH** | Check IP, try `ping <pi-ip>` |
| **Camera not detected** | Run `v4l2-ctl --list-devices` |
| **XDP filter won't load** | Check kernel version `uname -r` |
| **No network** | Check `ip addr`, `ping 8.8.8.8` |
| **High CPU** | Monitor with `htop`, check processes |

### eBPF Specific

```bash
# Check kernel supports eBPF
grep CONFIG_BPF /boot/config-$(uname -r)

# Check XDP support
ethtool -k eth0 | grep xdp

# View kernel logs
dmesg | grep -i xdp
dmesg | grep -i bpf

# Verify bpftool works
bpftool version
```

### SD Card Issues

```bash
# macOS: Verify write
diskutil list  # Should show SD card

# Windows: Verify write
# Use Win32DiskImager "Read" to verify

# Test SD card speed
sudo dd if=/dev/zero of=/tmp/test bs=1M count=1000 oflag=direct
```

---

## 📊 System Monitoring

### Real-time Monitoring

```bash
# Process viewer
htop

# Network stats
ip -s link show eth0

# CPU/Memory
top -b -n 1 | head -20

# eBPF program stats
bpftool prog show

# Disk usage
df -h
```

### Log Monitoring

```bash
# System logs
journalctl -f

# XDP drops
dmesg | tail -20

# Custom camera logs
tail -f /var/log/camera.log
```

---

## 🔄 Update the System

### Update Kernel

```bash
# Edit buildroot/.config
# Change BR2_LINUX_KERNEL_CUSTOM_VERSION_VALUE

# Rebuild
cd buildroot
make
```

### Add/Remove Packages

```bash
# Add a package
echo "BR2_PACKAGE_NEWPACKAGE=y" >> buildroot/.config
make olddefconfig
make
```

### Backup Configuration

```bash
# Save current config
cp buildroot/.config buildroot/.config.backup

# Save eBPF filters
cp /usr/local/sbin/*.bpf.o /home/pi/backup/

# Backup SD card
sudo dd if=/dev/mmcblk0 of=/tmp/pi-backup.img bs=1M
```

---

## 📚 Additional Resources

- **Buildroot Manual:** https://buildroot.org/downloads/manual/manual.html
- **Raspberry Pi Docs:** https://www.raspberrypi.com/documentation/
- **eBPF/XDP Reference:** https://docs.kernel.org/bpf/prog_xdp.html
- **GStreamer Docs:** https://gstreamer.freedesktop.org/documentation/
- **V4L2 API:** https://www.kernel.org/doc/html/latest/userspace-api/media/v4l/dev-dequeuebuf.html

---

## 🆘 Support

| Resource | Link |
|---|---|
| **Repository** | Your GitHub repo |
| **Issues** | GitHub Issues tab |
| **Documentation** | `SECURITY-CAMERA-BUILD-REPORT.md` |
| **Build Script** | `build.ps1` / `build-docker.sh` |

---

**Last Updated:** 2026-07-14  
**Version:** 1.0  
**Target:** Raspberry Pi 4 (ARM64)
