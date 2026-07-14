#!/bin/bash
# QEMU boot script for Airport eBPF Security Camera
# Usage: ./qemu-boot.sh

set -e

BUILD_DIR="$(cd "$(dirname "$0")/buildroot" && pwd)"
OUTPUT="$BUILD_DIR/output/images"

# Check for required files
if [ ! -f "$OUTPUT/Image" ]; then
    echo "❌ Kernel not found. Build first: cd buildroot && make qemu_aarch64_virt_defconfig && make"
    exit 1
fi

if [ ! -f "$OUTPUT/rootfs.ext4" ]; then
    echo "❌ Rootfs not found. Build first."
    exit 1
fi

# Create qcow2 disk image (if not exists)
DISK_IMAGE="$OUTPUT/rootfs.qcow2"
if [ ! -f "$DISK_IMAGE" ]; then
    echo "📦 Creating qcow2 disk image (8GB)..."
    qemu-img create -f qcow2 "$DISK_IMAGE" 8G
    
    # Format and populate disk
    echo "⏳ Formatting ext4 and copying files..."
    sudo modprobe nbd max_part=8 2>/dev/null || true
    sudo qemu-nbd -c /dev/nbd0 "$OUTPUT/rootfs.ext4" 2>/dev/null
    
    # Create filesystem and copy
    sleep 1
    sudo mkfs.ext4 -q /dev/nbd0
    sudo mount -o nosuid,nodev /dev/nbd0 /mnt
    
    # Copy rootfs contents
    sudo cp -a /bin /boot /etc /home /lib /opt /root /sbin /srv /tmp /usr /var /mnt/
    sudo cp -a /dev /mnt/ 2>/dev/null || true
    
    # Unmount and disconnect
    sudo umount /mnt
    sudo qemu-nbd -d /dev/nbd0 2>/dev/null || true
    echo "✅ Disk image created"
fi

# Boot QEMU
echo "🚀 Starting QEMU..."
qemu-system-aarch64 \
    -M virt,gic-version=host \
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
