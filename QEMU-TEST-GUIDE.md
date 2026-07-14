# 🐧 QEMU Testing Guide for Airport eBPF Security Camera

Complete guide for building and testing your embedded Linux image using **QEMU** virtualization.

---

## 📋 Overview

| Component | QEMU Configuration |
|---|---|
| **Architecture** | ARM64 (aarch64) |
| **Board** | QEMU virt (virtual platform) |
| **Kernel** | Linux 6.1.0 (generic ARM64) |
| **Storage** | qcow2 disk image |
| **Network** | User-mode networking (slirp) |
| **Console** | Serial port + optional VNC |

---

## 🚀 Quick Start

```bash
cd /Users/kiran/Projects/airport-ebpf-mac/buildroot

# 1. Switch to QEMU configuration
make qemu_aarch64_virt_defconfig

# 2. Enable camera packages (from .config)
# See section below for package selection

# 3. Build the image
make -j$(nproc)

# 4. Boot in QEMU (see scripts below)
```

---

## ⚙️ Step 1: Switch to QEMU Configuration

### Option A: Use Pre-configured QEMU Defconfig

```bash
cd buildroot
make qemu_aarch64_virt_defconfig
```

This automatically sets:
- ✅ ARM64 architecture
- ✅ Generic virt machine (no Pi hardware)
- ✅ Standard kernel defconfig
- ✅ Serial console

### Option B: Manual Configuration

```bash
make qemu_aarch64_virt_defconfig

# Then edit .config to ensure:
# BR2_aarch64=y
# BR2_LINUX_KERNEL_DEFCONFIG="arm64_virt"
```

---

## 📦 Step 2: Enable Camera Packages

After switching to QEMU config, re-enable your packages:

```bash
cd buildroot

# Add these to .config manually or via menuconfig
cat >> .config << 'EOF'

# === Camera & Multimedia ===
BR2_PACKAGE_FFMPEG=y
BR2_PACKAGE_GSTREAMER1=y
BR2_PACKAGE_GST1_PLUGINS_BASE=y
BR2_PACKAGE_GST1_PLUGINS_GOOD=y
BR2_PACKAGE_GST1_PLUGINS_BAD=y
BR2_PACKAGE_LIBV4L=y
BR2_PACKAGE_LIBV4L_UTILS=y
BR2_PACKAGE_LIBGPIOD=y
BR2_PACKAGE_LIBGPIOD_TOOLS=y

# === Networking ===
BR2_PACKAGE_LIGHTTPD=y
BR2_PACKAGE_LIGHTTPD_OPENSSL=y
BR2_PACKAGE_OPENSSH=y
BR2_PACKAGE_OPENSSH_SERVER=y
BR2_PACKAGE_OPENSSH_CLIENT=y
BR2_PACKAGE_IPUTILS=y

# === eBPF ===
BR2_PACKAGE_BPFTOOL=y
BR2_PACKAGE_LIBBPF=y
BR2_PACKAGE_AIRPORT_EBPF=y
BR2_PACKAGE_AIRPORT_EBPF_CMS=y
BR2_PACKAGE_AIRPORT_EBPF_ELASTIC=y

# === System ===
BR2_PACKAGE_BASH=y
BR2_PACKAGE_ZLIB=y
BR2_PACKAGE_SQUASHFS=y
BR2_PACKAGE_HTOP=y
BR2_PACKAGE_SYSSTAT=y
BR2_PACKAGE_E2FSPROGS=y
BR2_PACKAGE_SYSTEMD=y
EOF

# Apply changes
make olddefconfig
```

---

## 🔨 Step 3: Build for QEMU

```bash
cd buildroot

# Build everything
make -j$(nproc)

# Output will be in:
# output/images/
#   - Image (kernel)
#   - rootfs.ext4 (filesystem)
#   - *.dtb (device tree)
```

### Expected Build Time
- **First build**: 1-2 hours
- **Incremental**: 15-30 minutes

---

## 🖥️ Step 4: Boot in QEMU

### Create QEMU Boot Script

Save as `qemu-boot.sh`:

```bash
#!/bin/bash
# QEMU boot script for Airport eBPF Camera

set -e

BUILD_DIR="$(cd "$(dirname "$0")/buildroot" && pwd)"
OUTPUT="$BUILD_DIR/output/images"

# Check for required files
if [ ! -f "$OUTPUT/Image" ]; then
    echo "❌ Kernel not found. Build first: cd buildroot && make"
    exit 1
fi

if [ ! -f "$OUTPUT/rootfs.ext4" ]; then
    echo "❌ Rootfs not found. Build first."
    exit 1
fi

# Create qcow2 disk image (if not exists)
DISK_IMAGE="$OUTPUT/rootfs.qcow2"
if [ ! -f "$DISK_IMAGE" ]; then
    echo "📦 Creating qcow2 disk image..."
    qemu-img create -f qcow2 "$DISK_IMAGE" 8G
    
    # Format and copy rootfs
    sudo modprobe nbd max_part=8
    qemu-nbd -c /dev/nbd0 "$OUTPUT/rootfs.ext4"
    sudo mkfs.ext4 /dev/nbd0
    sudo mount /dev/nbd0 /mnt
    sudo cp -a /bin /boot /etc /home /lib /opt /root /sbin /srv /tmp /usr /var /mnt/
    sudo umount /mnt
    qemu-nbd -d /dev/nbd0
fi

# Boot QEMU
qemu-system-aarch64 \
    -M virt \
    -cpu cortex-a53 \
    -m 2G \
    -smp 2 \
    -kernel "$OUTPUT/Image" \
    -append "root=/dev/vda console=ttyAMA0 loglevel=8" \
    -drive "file=$DISK_IMAGE,format=qcow2,if=virtio" \
    -netdev user,id=net0,hostfwd=tcp::2222-:22 \
    -device virtio-net-pci,netdev=net0 \
    -serial stdio \
    -nographic
```

Make executable:
```bash
chmod +x qemu-boot.sh
```

### Boot Command

```bash
./qemu-boot.sh
```

---

## 🔍 Step 5: Test eBPF Filters

### Boot and Login

```
# Login credentials
Username: root
Password: (empty, just press Enter)
```

### Verify Installation

```bash
# Check eBPF tools
which bpftool
which libbpf

# List kernel modules
lsmod

# Check kernel eBPF support
cat /proc/config.gz | gunzip | grep CONFIG_BPF
```

### Load eBPF Filters

```bash
# Load CMS filter
bpftool prog load /usr/local/sbin/xdp_cms_filter.bpf.o /sys/fs/bpf/cms

# Load Elastic filter
bpftool prog load /usr/local/sbin/xdp_elastic_filter.bpf.o /sys/fs/bpf/elastic

# Verify loaded
bpftool prog list
```

### Network Testing

```bash
# Get network interface
ip link show

# Create test traffic
ping -c 5 8.8.8.8

# Check stats
cat /sys/kernel/debug/tracing/trace_pipe
```

---

## 🎯 Step 6: Advanced QEMU Testing

### Option A: With VNC (Visual Interface)

```bash
qemu-system-aarch64 \
    -M virt -cpu cortex-a53 -m 2G -smp 2 \
    -kernel buildroot/output/images/Image \
    -append "root=/dev/vda console=tty0" \
    -drive "file=buildroot/output/images/rootfs.qcow2,format=qcow2,if=virtio" \
    -netdev user,id=net0,hostfwd=tcp::2222-:22 \
    -device virtio-net-pci,netdev=net0 \
    -vnc :1 \
    -display cocoa
```

Connect with VNC client to `localhost:5901`

### Option B: Network Testing Setup

```bash
# On QEMU host (your Mac)
nc -l 8080 | nc -l 8081

# In QEMU guest
curl http://host-gateway:8080  # If using host networking
```

### Option C: File Transfer

```bash
# From QEMU guest to host
scp -P 2222 file.txt kiran@localhost:/tmp/

# From host to QEMU guest
scp -P 2222 file.txt root@localhost:/tmp/
```

---

## 🔧 Troubleshooting

### Common Issues

| Problem | Solution |
|---|---|
| **"Kernel not found"** | Run `make` first in buildroot |
| **"Can't find rootfs"** | Check `output/images/` for `rootfs.ext4` |
| **QEMU crashes** | Try `-cpu max` instead of `-cpu cortex-a53` |
| **No network** | Check `-netdev user` configuration |
| **eBPF load fails** | Verify kernel has `CONFIG_BPF=y` |

### Kernel Configuration Check

```bash
# In QEMU guest
zcat /proc/config.gz | grep BPF
# Should show: CONFIG_BPF=y
# Should show: CONFIG_BPF_SYSCALL=y
# Should show: CONFIG_NET_ACT_BPF=y
```

### Network Diagnostics

```bash
# In QEMU guest
ip addr
ip route
cat /etc/resolv.conf

# Test DNS
nslookup google.com

# Test connectivity
ping -c 3 google.com
```

---

## 📊 Performance Comparison

| Platform | Boot Time | CPU Usage | Network |
|---|---|---|---|
| **QEMU** | 30-60s | 50-100% (emulation) | 100-200 Mbps |
| **Raspberry Pi** | 15-30s | 10-20% | 500-1000 Mbps |
| **Native Linux** | 5-15s | 5-10% | 1-10 Gbps |

> ⚠️ QEMU emulation is 5-10x slower than native hardware.

---

## 🔄 Alternative: Using Docker

If QEMU is too slow, use Docker with QEMU:

```bash
# Install QEMU static binaries
brew install multiarch/qemu/simulator

# Use Docker with emulation
docker run --rm -it \
    -v $(pwd)/buildroot:/build \
    --platform linux/arm64 \
    ubuntu:22.04 \
    /bin/bash
```

---

## 📚 Additional Resources

- **QEMU ARM64 Docs:** https://wiki.qemu.org/Hosts/ARM
- **Linux virt Machine:** https://www.kernel.org/doc/Documentation/arm/virt.txt
- **eBPF in Containers:** https://docs.kernel.org/bpf/index.html

---

**Last Updated:** 2026-07-14  
**Target:** QEMU ARM64 virt machine  
**Buildroot:** 2026.05
