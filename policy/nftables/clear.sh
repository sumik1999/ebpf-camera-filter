#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
# shellcheck source=/dev/null
source "${ROOT_DIR}/scripts/lib/load-config.sh"

if [[ "${EUID}" -ne 0 ]]; then
  echo "Run with sudo: sudo $0"
  exit 1
fi

if ip netns list | awk '{print $1}' | grep -qx "${NS_GUARD}"; then
  ip netns exec "${NS_GUARD}" nft delete table inet airport_guard >/dev/null 2>&1 || true
fi
echo "nftables guard cleared"

