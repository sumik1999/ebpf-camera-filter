#!/usr/bin/env bash
# Start the RAPID network-segmentation controller on an existing topology.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TOPOLOGY="${RAPID_TOPOLOGY:-${ROOT_DIR}/config/rapid_topology.json}"
INTERVAL="${RAPID_INTERVAL_SEC:-0.5}"
MIN_PACKETS="${RAPID_MIN_PACKETS:-50}"
CONFIRM_WINDOWS="${RAPID_CONFIRM_WINDOWS:-2}"
OUTPUT_DIR="${RAPID_OUTPUT_DIR:-${ROOT_DIR}/eval/results/rapid_live}"

[[ "${EUID}" -ne 0 ]] && { echo "Run with sudo: sudo $0"; exit 1; }
[[ "$(uname -s)" != "Linux" ]] && { echo "RAPID requires Linux switch interfaces."; exit 1; }
[[ -f "${TOPOLOGY}" ]] || { echo "Missing topology: ${TOPOLOGY}"; exit 1; }

for iface in v_apoc_br v_att_br v_gterm_br v_gcam_br; do
    ip link show "${iface}" >/dev/null 2>&1 || {
        echo "Missing ${iface}; run 'sudo make setup' first."
        exit 1
    }
done

mkdir -p "${OUTPUT_DIR}"
exec python3 "${ROOT_DIR}/eval/rapid_adaptive_controller.py" \
    --topology "${TOPOLOGY}" \
    --interval "${INTERVAL}" \
    --minimum-packets "${MIN_PACKETS}" \
    --confirmation-windows "${CONFIRM_WINDOWS}" \
    --cleanup-on-exit \
    --csv-out "${OUTPUT_DIR}/rapid_metrics.csv" \
    --events-out "${OUTPUT_DIR}/rapid_events.json"
