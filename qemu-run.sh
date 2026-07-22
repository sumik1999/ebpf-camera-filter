#!/bin/sh
set -eu

cd "$(dirname "$0")"

for file in sdcard.img Image bcm2711-rpi-4-b.dtb; do
    if [ ! -r "output-images/$file" ]; then
        echo "Missing output-images/$file; build the OS image first." >&2
        exit 2
    fi
done

exec docker compose -f docker-compose.qemu.yml run --rm rpi4 "$@"
