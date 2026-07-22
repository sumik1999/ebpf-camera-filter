#!/bin/sh
set -eu

cd "$(dirname "$0")"
docker compose -f docker-compose.qemu.yml run --rm \
    -e RESET_SD=1 -e INIT_ONLY=1 rpi4
