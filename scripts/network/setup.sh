#!/usr/bin/env bash
# setup_ns_tree.sh – Create the simulated airport network topology.
#
# Mac / Lima adaptation
# ─────────────────────
# Functionally identical to the WSL2 version, but includes:
#   • A pre-flight check verifying the script runs inside a Linux
#     environment (Lima guest, OrbStack VM, or Docker) rather than
#     directly on macOS (where "ip netns" would not exist).
#   • Explicit MTU alignment on veth pairs – XDP in SKB/generic mode
#     requires the MTU to match between peer interfaces so the XDP
#     data_end pointer is consistent.
#   • Disables IPv6 on all namespaces to silence router-solicitation
#     noise that can pollute eBPF packet counters.
#   • Enables RXVLAN offload off / GRO off on guard interface so that
#     XDP sees individual packets rather than aggregated segments.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
# shellcheck source=/dev/null
source "${ROOT_DIR}/scripts/lib/load-config.sh"

# ── Pre-flight: must run inside Linux ───────────────────────────────────
if [[ "$(uname -s)" != "Linux" ]]; then
    echo "ERROR: This script must run inside a Linux VM (Lima / OrbStack)."
    echo "       On macOS, open a Lima shell:  limactl shell default"
    echo "       or OrbStack shell:            orb"
    exit 1
fi

require_root() {
    if [[ "${EUID}" -ne 0 ]]; then
        echo "setup_ns_tree.sh requires root. Run: sudo $0"
        exit 1
    fi
}

run_ns() { ip netns exec "$1" "${@:2}"; }

create_ns() {
    local ns="$1"
    ip netns add "${ns}"
    run_ns "${ns}" ip link set lo up
    # Silence IPv6 to keep eBPF packet counters clean
    run_ns "${ns}" sysctl -w net.ipv6.conf.all.disable_ipv6=1 >/dev/null 2>&1 || true
    run_ns "${ns}" sysctl -w net.ipv6.conf.default.disable_ipv6=1 >/dev/null 2>&1 || true
}

bridge_add() {
    local bridge="$1"
    ip link add name "${bridge}" type bridge
    ip link set "${bridge}" up
}

veth_to_ns() {
    local host_if="$1" ns_if="$2" ns="$3" bridge="$4"
    ip link add "${host_if}" type veth peer name "${ns_if}" netns "${ns}"
    # Set MTU to 1500 on both sides – required for XDP SKB-mode consistency
    ip link set "${host_if}" mtu 1500
    run_ns "${ns}" ip link set "${ns_if}" mtu 1500
    ip link set "${host_if}" master "${bridge}"
    ip link set "${host_if}" up
    run_ns "${ns}" ip link set "${ns_if}" up
}

set_addr() {
    local ns="$1" iface="$2" cidr="$3"
    run_ns "${ns}" ip addr add "${cidr}" dev "${iface}"
}

xdp_offload_prep() {
    # Disable GRO / GRO-HW and rx-vlan offload on the guard's terminal
    # interface so XDP sees raw individual frames.
    # ethtool may not be present in all VMs; ignore failures gracefully.
    ip netns exec "${NS_GUARD}" ethtool -K "${GUARD_TERMINAL_IF}" \
        gro off gro-hw off rx-vlan-offload off 2>/dev/null || true
}

camera_ip() {
    local idx="$1" var="CAMERA${idx}_IP"
    printf '%s' "${!var}"
}

verify_ping() {
    local from_ns="$1" target="$2" label="$3"
    if run_ns "${from_ns}" ping -c 1 -W 1 "${target}" >/dev/null 2>&1; then
        printf '[ OK ] %s -> %s (%s)\n' "${from_ns}" "${target}" "${label}"
    else
        printf '[FAIL] %s -> %s (%s)\n' "${from_ns}" "${target}" "${label}"
        return 1
    fi
}

# ── Main setup ───────────────────────────────────────────────────────────
require_root
"${ROOT_DIR}/scripts/network/cleanup.sh" >/dev/null 2>&1 || true

echo "Creating network namespaces …"
create_ns "${NS_APOC}"
create_ns "${NS_ATTACKER}"
create_ns "${NS_GUARD}"
for idx in 1 2 3 4; do create_ns "ns_camera${idx}"; done

echo "Creating bridges …"
bridge_add "${BR_TERMINAL}"
bridge_add "${BR_CAMERA}"

echo "Creating veth pairs …"
veth_to_ns v_apoc_br   "${APOC_IF}"           "${NS_APOC}"      "${BR_TERMINAL}"
veth_to_ns v_att_br    "${ATTACKER_IF}"        "${NS_ATTACKER}"  "${BR_TERMINAL}"
veth_to_ns v_gterm_br  "${GUARD_TERMINAL_IF}"  "${NS_GUARD}"     "${BR_TERMINAL}"
veth_to_ns v_gcam_br   "${GUARD_CAMERA_IF}"    "${NS_GUARD}"     "${BR_CAMERA}"
for idx in 1 2 3 4; do
    veth_to_ns "v_cam${idx}_br" "${CAMERA_IF}" "ns_camera${idx}" "${BR_CAMERA}"
done

echo "Assigning IP addresses …"
set_addr "${NS_APOC}"     "${APOC_IF}"          "${APOC_IP}/24"
set_addr "${NS_ATTACKER}" "${ATTACKER_IF}"       "${ATTACKER_IP}/24"
set_addr "${NS_GUARD}"    "${GUARD_TERMINAL_IF}" "${GUARD_TERMINAL_IP}/24"
set_addr "${NS_GUARD}"    "${GUARD_CAMERA_IF}"   "${GUARD_CAMERA_IP}/24"
for idx in 1 2 3 4; do
    set_addr "ns_camera${idx}" "${CAMERA_IF}" "$(camera_ip "${idx}")/24"
done

echo "Configuring routing …"
run_ns "${NS_GUARD}"    sysctl -w net.ipv4.ip_forward=1 >/dev/null
run_ns "${NS_GUARD}"    sysctl -w net.ipv4.conf.all.rp_filter=0     >/dev/null 2>&1 || true
run_ns "${NS_GUARD}"    sysctl -w net.ipv4.conf.default.rp_filter=0 >/dev/null 2>&1 || true

run_ns "${NS_APOC}"     ip route add default via "${GUARD_TERMINAL_IP}"
run_ns "${NS_ATTACKER}" ip route add default via "${GUARD_TERMINAL_IP}"
for idx in 1 2 3 4; do
    run_ns "ns_camera${idx}" ip route add default via "${GUARD_CAMERA_IP}"
done

# XDP-specific NIC offload adjustments on the guard interface
xdp_offload_prep

echo ""
echo "== Connectivity verification =="
for idx in 1 2 3 4; do
    verify_ping "${NS_APOC}"     "$(camera_ip "${idx}")" "APOC -> camera${idx}"
done
for idx in 1 2 3 4; do
    verify_ping "${NS_ATTACKER}" "$(camera_ip "${idx}")" "attacker -> camera${idx}"
done
for idx in 1 2 3 4; do
    verify_ping "ns_camera${idx}" "${APOC_IP}" "camera${idx} -> APOC"
done

echo ""
"${ROOT_DIR}/scripts/network/show-topology.sh" 2>/dev/null || true
echo "Topology ready."
