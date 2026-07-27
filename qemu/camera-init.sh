#!/bin/sh

# QEMU cannot emulate the Raspberry Pi CSI pipeline. Expose deterministic Y4M
# recordings at the paths used by camera integration tests instead.
case "${1:-start}" in
    start)
        for camera in 1 2 3 4; do
            ln -snf "/opt/qemu-camera/vid${camera}.y4m" "/dev/vid${camera}"
        done
        ;;
esac
