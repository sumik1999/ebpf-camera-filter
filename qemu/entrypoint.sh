#!/bin/sh
set -eu

IMAGE_DIR=${IMAGE_DIR:-/images}
STATE_DIR=${STATE_DIR:-/state}
SD_SOURCE=${SD_SOURCE:-$IMAGE_DIR/sdcard.img}
KERNEL=${KERNEL:-$IMAGE_DIR/Image}
DTB=${DTB:-$IMAGE_DIR/bcm2711-rpi-4-b.dtb}
SD_WORK=${SD_WORK:-$STATE_DIR/sdcard-rpi4.img}
SD_SIZE=${SD_SIZE:-2G}
# With the upstream Pi 4 DTB, QEMU's PL011 appears as ttyAMA1.
CONSOLE=${CONSOLE:-ttyAMA1}
SERIAL_BACKEND=${SERIAL_BACKEND:-stdio}

for file in "$SD_SOURCE" "$KERNEL" "$DTB"; do
    if [ ! -r "$file" ]; then
        echo "qemu-rpi4: required file is not readable: $file" >&2
        exit 2
    fi
done

mkdir -p "$STATE_DIR"

if [ "${RESET_SD:-0}" = 1 ]; then
    echo "Removing writable SD card $SD_WORK"
    rm -f "$SD_WORK"
fi

# QEMU's SD model requires a power-of-two card capacity. Buildroot's image is
# partition-sized, so make a writable, padded copy while preserving the source.
if [ ! -e "$SD_WORK" ]; then
    echo "Initializing writable SD card at $SD_WORK from $SD_SOURCE"
    cp --sparse=always "$SD_SOURCE" "$SD_WORK"
    truncate -s "$SD_SIZE" "$SD_WORK"
    if [ "${INJECT_CAMERA_VIDEOS:-1}" = 1 ]; then
        inject-camera-videos.sh "$SD_WORK"
    fi
fi

if [ "${INIT_ONLY:-0}" = 1 ]; then
    echo "Writable SD card is ready."
    exit 0
fi

case "$(stat -c %s "$SD_WORK")" in
    268435456|536870912|1073741824|2147483648|4294967296|8589934592|17179869184|34359738368|68719476736|137438953472|274877906944) ;;
    *)
        echo "qemu-rpi4: SD_WORK must have a power-of-two size; remove it and use SD_SIZE=2G" >&2
        exit 2
        ;;
esac

# The emulated SD controller enumerates as mmcblk1 with this DTB.
APPEND=${KERNEL_APPEND:-"root=/dev/mmcblk1p2 rootwait rw console=$CONSOLE,115200 loglevel=7"}

if [ "${QEMU_DISPLAY:-none}" = none ]; then
    DISPLAY_ARGS="-display none"
else
    DISPLAY_ARGS="-display ${QEMU_DISPLAY}"
fi

# raspi4b fixes the virtual board at four Cortex-A72 CPUs and 2 GiB RAM.
# Extra arguments can be supplied after the image name.
# shellcheck disable=SC2086
exec qemu-system-aarch64 \
    -name camera-os-rpi4 \
    -machine raspi4b \
    -kernel "$KERNEL" \
    -dtb "$DTB" \
    -drive "file=$SD_WORK,if=sd,format=raw" \
    -append "$APPEND" \
    -serial "$SERIAL_BACKEND" \
    -monitor none \
    $DISPLAY_ARGS \
    -no-reboot \
    "$@"
