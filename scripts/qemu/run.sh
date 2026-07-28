#!/bin/sh
set -eu

ROOT_DIR=$(cd "$(dirname "$0")/../.." && pwd)
cd "$ROOT_DIR"

for file in sdcard.img Image bcm2711-rpi-4-b.dtb; do
    if [ ! -r "output-images/$file" ]; then
        echo "Missing output-images/$file; build the OS image first." >&2
        exit 2
    fi
done

exec docker compose -f containers/docker-compose.qemu.yml run --rm rpi4 "$@"
