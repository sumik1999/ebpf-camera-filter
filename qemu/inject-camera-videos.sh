#!/bin/sh
set -eu

sd_image=${1:?usage: inject-camera-videos.sh SD_IMAGE}
assets=${CAMERA_ASSETS:-/usr/share/qemu-camera}
work=$(mktemp -d /tmp/qemu-camera.XXXXXX)
trap 'rm -rf "$work"' EXIT INT TERM

# The Buildroot image's second MBR partition is the ext4 root filesystem.
partition=$(partx --raw --noheadings --output START,SECTORS --nr 2 "$sd_image")
set -- $partition
if [ "$#" -ne 2 ] || [ "$1" -le 0 ] || [ "$2" -le 0 ]; then
    echo "qemu-rpi4: could not locate root partition 2 in $sd_image" >&2
    exit 2
fi
start=$1
sectors=$2
rootfs=$work/rootfs.ext4

echo "Installing four prerecorded camera streams in the writable SD image"
dd if="$sd_image" of="$rootfs" bs=512 skip="$start" count="$sectors" status=none

# Decompress outside the filesystem, then use debugfs so this remains
# unprivileged and works in Docker Desktop on every supported host.
for camera in 1 2 3 4; do
    gzip -dc "$assets/vid${camera}.y4m.gz" >"$work/vid${camera}.y4m"
done

commands=$work/debugfs.commands
{
    echo 'mkdir /opt/qemu-camera'
    for camera in 1 2 3 4; do
        echo "write $work/vid${camera}.y4m /opt/qemu-camera/vid${camera}.y4m"
    done
    echo "write /usr/local/lib/qemu-camera-init /etc/init.d/S01qemu-camera"
    echo 'set_inode_field /etc/init.d/S01qemu-camera mode 0100755'
    echo "write /usr/local/lib/guest-camera-network-test.sh /opt/qemu-camera/network-test.sh"
    echo 'set_inode_field /opt/qemu-camera/network-test.sh mode 0100755'
    echo "write /usr/local/lib/camera-network-test-init.sh /etc/init.d/S99qemu-camera-test"
    echo 'set_inode_field /etc/init.d/S99qemu-camera-test mode 0100755'
    echo "write /usr/local/lib/guest-wifi-camera-test.sh /opt/qemu-camera/wifi-network-test.sh"
    echo 'set_inode_field /opt/qemu-camera/wifi-network-test.sh mode 0100755'
    echo "write /usr/local/lib/wifi-camera-test-init.sh /etc/init.d/S99qemu-wifi-camera-test"
    echo 'set_inode_field /etc/init.d/S99qemu-wifi-camera-test mode 0100755'
} >"$commands"

debugfs -w -f "$commands" "$rootfs" >/dev/null
# Write only the root partition back; the source image and boot partition stay
# untouched. conv=notrunc preserves the padded, power-of-two SD capacity.
dd if="$rootfs" of="$sd_image" bs=512 seek="$start" conv=notrunc status=none
