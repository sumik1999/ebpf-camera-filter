#!/usr/bin/env bash
# cleanup_ns.sh – Tear down the simulated airport topology.
# Mac / Lima: also removes XDP programs (in addition to TC qdiscs).

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck source=/dev/null
source "${ROOT_DIR}/config.env"

[[ "${EUID}" -ne 0 ]] && { echo "Run with sudo: sudo $0"; exit 1; }

ns_exists() { ip netns list | awk '{print $1}' | grep -qx "$1"; }
try_ns() {
    local ns="$1"; shift
    ns_exists "${ns}" && ip netns exec "${ns}" "$@" >/dev/null 2>&1 || true
}

namespaces=(
    "${NS_APOC}" "${NS_ATTACKER}" "${NS_GUARD}"
    ns_camera1 ns_camera2 ns_camera3 ns_camera4
)

# Kill any lingering traffic processes
for ns in "${namespaces[@]}"; do
    try_ns "${ns}" pkill -f \
        'telemetry_server.py|telemetry_client.py|udp_flood.py|syn_flood.py|camera_service.py'
done

# Detach XDP programs (Mac primary mode)
try_ns "${NS_GUARD}" ip link set dev "${GUARD_TERMINAL_IF}" xdp off
try_ns "${NS_GUARD}" ip link set dev "${GUARD_CAMERA_IF}"   xdp off
for idx in 1 2 3 4; do
    try_ns "ns_camera${idx}" ip link set dev "${CAMERA_IF}" xdp off
done

# Detach TC clsact qdiscs (TC fallback mode)
try_ns "${NS_GUARD}" tc qdisc del dev "${GUARD_TERMINAL_IF}" clsact
try_ns "${NS_GUARD}" tc qdisc del dev "${GUARD_CAMERA_IF}"   clsact
for idx in 1 2 3 4; do
    try_ns "ns_camera${idx}" tc qdisc del dev "${CAMERA_IF}" clsact
done

# Remove nftables rules
try_ns "${NS_GUARD}" nft delete table inet airport_guard

# Delete namespaces
for ns in "${namespaces[@]}"; do
    ns_exists "${ns}" && ip netns del "${ns}" || true
done

# Delete bridges
for bridge in "${BR_TERMINAL}" "${BR_CAMERA}"; do
    if ip link show "${bridge}" >/dev/null 2>&1; then
        ip link set "${bridge}" down || true
        ip link del "${bridge}"  || true
    fi
done

rm -rf /tmp/airport-ebpf-defense
echo "Cleaned airport namespace topology and temporary state."
