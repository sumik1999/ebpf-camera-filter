#!/bin/sh
set -eu

ROOT_DIR=$(cd "$(dirname "$0")/../.." && pwd)
cd "$ROOT_DIR"
docker compose -f containers/docker-compose.qemu.yml run --rm \
    -e RESET_SD=1 -e INIT_ONLY=1 rpi4
