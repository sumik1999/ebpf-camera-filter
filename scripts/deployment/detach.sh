#!/usr/bin/env bash
# detach_xdp.sh – Remove XDP program and any TC clsact qdisc from guard.

set -euo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
source "${ROOT_DIR}/scripts/lib/load-config.sh"

[[ "${EUID}" -ne 0 ]] && { echo "Run with sudo."; exit 1; }

# Remove XDP attachment
ip netns exec "${NS_GUARD}" \
    ip link set dev "${GUARD_TERMINAL_IF}" xdp off 2>/dev/null || true

# Remove TC clsact qdisc if present (left by TC fallback)
ip netns exec "${NS_GUARD}" \
    tc qdisc del dev "${GUARD_TERMINAL_IF}" clsact 2>/dev/null || true

echo "XDP (and TC fallback) detached from ${NS_GUARD}/${GUARD_TERMINAL_IF}"
