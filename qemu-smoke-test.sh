#!/bin/sh
set -eu

cd "$(dirname "$0")"

for file in sdcard.img Image bcm2711-rpi-4-b.dtb; do
    test -r "output-images/$file" || {
        echo "Missing output-images/$file; build the OS image first." >&2
        exit 2
    }
done

docker compose -f docker-compose.qemu.yml build >/dev/null

name="camera-os-qemu-smoke-$$"
state=$(mktemp -d "${TMPDIR:-/tmp}/camera-os-qemu.XXXXXX")
cid=
cleanup() {
    if [ -n "$cid" ]; then
        docker rm -f "$cid" >/dev/null 2>&1 || true
    fi
    rm -rf "$state"
}
trap cleanup EXIT INT TERM

cid=$(docker run -d --name "$name" \
    -e SERIAL_BACKEND=file:/state/console.log \
    -v "$(pwd)/output-images:/images:ro" \
    -v "$state:/state" \
    camera-os-qemu-rpi4:latest)

timeout=${QEMU_SMOKE_TIMEOUT:-90}
i=0
while [ "$i" -lt "$timeout" ]; do
    if grep -q 'camera-os login:' "$state/console.log" 2>/dev/null; then
        grep -q 'Machine model: Raspberry Pi 4 Model B' "$state/console.log"
        grep -q 'Mounted root (ext4 filesystem)' "$state/console.log"
        echo "PASS: Camera OS reached login on the emulated Raspberry Pi 4."
        exit 0
    fi
    if ! docker inspect -f '{{.State.Running}}' "$cid" 2>/dev/null | grep -q true; then
        docker logs "$cid" 2>&1 || true
        tail -100 "$state/console.log" 2>/dev/null || true
        echo "FAIL: QEMU exited before the login prompt." >&2
        exit 1
    fi
    i=$((i + 1))
    sleep 1
done

tail -100 "$state/console.log" 2>/dev/null || true
echo "FAIL: login prompt not reached in ${timeout}s." >&2
exit 1
