#!/usr/bin/env bash
# show_topology.sh – Print the simulated airport network topology table.
# Unchanged from WSL2 version; works identically on Mac/Lima.

set -euo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck source=/dev/null
source "${ROOT_DIR}/config.env"

printf '%-14s %-18s %-16s %-16s %-20s\n' \
    "Namespace" "Interface" "IP" "Bridge/Subnet" "Default route"
printf '%-14s %-18s %-16s %-16s %-20s\n' \
    "---------" "---------" "--" "-------------" "-------------"
printf '%-14s %-18s %-16s %-16s %-20s\n' \
    "${NS_APOC}"     "${APOC_IF}"          "${APOC_IP}/24"          "${BR_TERMINAL}" "via ${GUARD_TERMINAL_IP}"
printf '%-14s %-18s %-16s %-16s %-20s\n' \
    "${NS_ATTACKER}" "${ATTACKER_IF}"       "${ATTACKER_IP}/24"      "${BR_TERMINAL}" "via ${GUARD_TERMINAL_IP}"
printf '%-14s %-18s %-16s %-16s %-20s\n' \
    "${NS_GUARD}"    "${GUARD_TERMINAL_IF}" "${GUARD_TERMINAL_IP}/24" "${BR_TERMINAL}" "forwarding (XDP hook)"
printf '%-14s %-18s %-16s %-16s %-20s\n' \
    "${NS_GUARD}"    "${GUARD_CAMERA_IF}"   "${GUARD_CAMERA_IP}/24"  "${BR_CAMERA}"   "forwarding"
for idx in 1 2 3 4; do
    ip_var="CAMERA${idx}_IP"
    printf '%-14s %-18s %-16s %-16s %-20s\n' \
        "ns_camera${idx}" "${CAMERA_IF}" "${!ip_var}/24" "${BR_CAMERA}" "via ${GUARD_CAMERA_IP}"
done
