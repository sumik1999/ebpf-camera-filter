#!/bin/sh
set -eu

ROOT_DIR=$(cd "$(dirname "$0")/../.." && pwd)
cd "$ROOT_DIR"

for file in sdcard.img Image bcm2711-rpi-4-b.dtb; do
    test -r "output-images/$file" || {
        echo "Missing output-images/$file; build the OS image first." >&2
        exit 2
    }
done

docker compose -f containers/docker-compose.qemu.yml build >/dev/null

name="camera-os-qemu-network-test-$$"
state=$(mktemp -d "${TMPDIR:-/tmp}/camera-os-qemu-network.XXXXXX")
cleanup()
{
    docker rm -f "$name" >/dev/null 2>&1 || true
    rm -rf "$state"
}
trap cleanup EXIT INT TERM

echo "Starting Raspberry Pi 4 QEMU test instance..."
cid=$(docker run -d --name "$name" \
    --user "$(id -u):$(id -g)" \
    -e SERIAL_BACKEND=file:/state/console.log \
    -e 'KERNEL_APPEND=root=/dev/mmcblk1p2 rootwait rw console=ttyAMA1,115200 loglevel=7 qemu_camera_test=1' \
    -v "$(pwd)/output-images:/images:ro" \
    -v "$state:/state" \
    camera-os-qemu-rpi4:latest)

console="$state/console.log"
timeout=${QEMU_CAMERA_TEST_TIMEOUT:-180}
elapsed=0
while test "$elapsed" -lt "$timeout"; do
    if grep -q '__QEMU_CAMERA_TEST_DONE__:' "$console" 2>/dev/null; then
        break
    fi
    if ! docker inspect -f '{{.State.Running}}' "$cid" 2>/dev/null | grep -q true; then
        break
    fi
    sleep 1
    elapsed=$((elapsed + 1))
done

echo
echo "=== Guest camera-network test output ==="
awk '/__QEMU_CAMERA_TEST_START__/{show=1} show{print}' "$console" 2>/dev/null || true

if grep -q '__QEMU_CAMERA_TEST_DONE__:0' "$console" 2>/dev/null; then
    echo
echo "PASS: current Camera OS image completed the four-camera QEMU test."
    echo "NOTE: QEMU raspi4b has no emulated GENET NIC or CSI camera. This test"
    echo "uses four prerecorded inputs and four isolated UDP endpoints on guest loopback."
    exit 0
fi

echo >&2
echo "FAIL: four-camera test did not pass within ${timeout}s." >&2
tail -100 "$console" >&2 2>/dev/null || docker logs "$cid" >&2 2>&1 || true
exit 1
