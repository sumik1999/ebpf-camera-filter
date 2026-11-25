#!/usr/bin/env bash
# Compile, pin, and attach the RAPID T-or-N TC/eBPF dataplane.
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OBJ="/tmp/rapid_port_guard.bpf.o"
PIN_ROOT="${RAPID_BPF_PIN_ROOT:-/sys/fs/bpf/airport_rapid}"
PROG_DIR="${PIN_ROOT}/programs"
MAP_DIR="${PIN_ROOT}/maps"
WINDOW_NS="${RAPID_WINDOW_NS:-500000000}"
PACKET_TRIGGER="${RAPID_PACKET_TRIGGER:-1000}"
MIN_PACKETS="${RAPID_MIN_PACKETS:-50}"
DELTA2_MILLI="${RAPID_DELTA2_MILLI:-500}"

[[ "${EUID}" -eq 0 ]] || { echo "Run as root"; exit 1; }
for tool in clang bpftool tc; do command -v "$tool" >/dev/null || { echo "Missing $tool"; exit 1; }; done

case "$(uname -m)" in
    aarch64|arm64) ARCH=arm64; TRIPLE=aarch64-linux-gnu ;;
    x86_64) ARCH=x86; TRIPLE=x86_64-linux-gnu ;;
    *) echo "Unsupported architecture: $(uname -m)"; exit 1 ;;
esac

INCLUDES=(-I"${ROOT_DIR}/ebpf" -I/usr/include)
[[ -d "/usr/include/${TRIPLE}" ]] && INCLUDES+=(-I"/usr/include/${TRIPLE}")
clang -O2 -g -target bpf -D"__TARGET_ARCH_${ARCH}" \
    -DRAPID_WINDOW_NS="${WINDOW_NS}ULL" \
    -DRAPID_PACKET_TRIGGER="${PACKET_TRIGGER}ULL" \
    -DRAPID_MIN_PACKETS="${MIN_PACKETS}ULL" \
    -DRAPID_DELTA2_MILLI="${DELTA2_MILLI}" \
    "${INCLUDES[@]}" -c "${ROOT_DIR}/ebpf/rapid_port_guard.bpf.c" -o "${OBJ}"

mountpoint -q /sys/fs/bpf || mount -t bpf bpf /sys/fs/bpf
rm -rf "${PIN_ROOT}"
rm -f /sys/fs/bpf/port_stats /sys/fs/bpf/blocked_ports \
      /sys/fs/bpf/guarded_ports /sys/fs/bpf/rapid_events
mkdir -p "${PROG_DIR}" "${MAP_DIR}"
bpftool prog loadall "${OBJ}" "${PROG_DIR}" pinmaps "${MAP_DIR}"

mapfile -t INTERFACES < <(python3 - "${ROOT_DIR}/config/rapid_topology.json" <<'PY'
import json, sys
data=json.load(open(sys.argv[1]))
for switch in data['switches']:
    for port in switch['ports']:
        if not port.get('namespace'):
            print(port['interface'])
PY
)

mapfile -t GUARDED_INTERFACES < <(python3 - "${ROOT_DIR}/config/rapid_topology.json" <<'PY'
import json, sys
data=json.load(open(sys.argv[1]))
for switch in data['switches']:
    for port in switch['ports']:
        if (port['kind'] == 'edge' and not port.get('trusted', False)
                and not port.get('namespace')):
            print(port['interface'])
PY
)
for iface in "${GUARDED_INTERFACES[@]}"; do
    ifindex="$(cat "/sys/class/net/${iface}/ifindex")"
    read -r -a key_bytes <<< "$(python3 -c 'import struct,sys; print(struct.pack("=I", int(sys.argv[1])).hex(" "))' "${ifindex}")"
    bpftool map update pinned "${MAP_DIR}/guarded_ports" \
        key hex "${key_bytes[@]}" value hex 01
done

for iface in "${INTERFACES[@]}"; do
    ip link show dev "${iface}" >/dev/null
    tc qdisc replace dev "${iface}" clsact
    tc filter replace dev "${iface}" ingress pref 1 \
        bpf da pinned "${PROG_DIR}/rapid_ingress"
    tc filter replace dev "${iface}" egress pref 1 \
        bpf da pinned "${PROG_DIR}/rapid_egress"
done

cat <<EOF
mode=rapid_tc_bpf_attached
window_ns=${WINDOW_NS}
packet_trigger=${PACKET_TRIGGER}
minimum_packets=${MIN_PACKETS}
delta2_milli=${DELTA2_MILLI}
pin_root=${PIN_ROOT}
interfaces=${INTERFACES[*]}
guarded_interfaces=${GUARDED_INTERFACES[*]}
EOF
