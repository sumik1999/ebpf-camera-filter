#!/usr/bin/env bash
# run_experiment.sh – Run one defense-case experiment.
# Mac / Lima: attach_xdp.sh replaces attach_tc.sh as the primary hook.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
# shellcheck source=/dev/null
source "${ROOT_DIR}/scripts/lib/load-config.sh"

CASE="${CASE:-no_defense}"
ATTACK_TYPE="${ATTACK_TYPE:-udp}"
TARGET_MODE="${TARGET_MODE:-${ATTACK_TARGET_MODE}}"
TEST_DURATION_SEC="${TEST_DURATION_SEC:-30}"
ATTACK_PPS="${ATTACK_PPS:-}"
RUN_ID="${RUN_ID:-$(date +%Y%m%d_%H%M%S)_${CASE}_${ATTACK_TYPE}_${TARGET_MODE}}"
RUN_DIR="${ROOT_DIR}/eval/results/${RUN_ID}"
CAMERA_TARGETS="${CAMERA1_IP},${CAMERA2_IP},${CAMERA3_IP},${CAMERA4_IP}"
mkdir -p "${RUN_DIR}"

[[ "${EUID}" -ne 0 ]] && { echo "Run with sudo: sudo CASE=${CASE} $0"; exit 1; }

cleanup_run() {
    set +e
    for pid in ${PIDS:-}; do kill "${pid}" >/dev/null 2>&1 || true; done
    [[ -n "${MONITOR_PID:-}" ]] && kill "${MONITOR_PID}" >/dev/null 2>&1 || true
    "${ROOT_DIR}/scripts/deployment/detach.sh"         >/dev/null 2>&1 || true
    "${ROOT_DIR}/policy/nftables/clear.sh"      >/dev/null 2>&1 || true
    "${ROOT_DIR}/scripts/network/cleanup.sh"          >/dev/null 2>&1 || true
}
trap cleanup_run EXIT

PIDS="" MONITOR_PID="" ABORT_FILE="${RUN_DIR}/auto_stop.flag"

cpu_percent_once() {
    awk '
      NR==1 { idle1=$5; total1=0; for (i=2;i<=NF;i++) total1+=$i }
      NR==2 { idle2=$5; total2=0; for (i=2;i<=NF;i++) total2+=$i
        dt=total2-total1; di=idle2-idle1
        if (dt>0) printf "%.0f\n",(100*(dt-di)/dt); else print 0 }
    ' <(grep "^cpu " /proc/stat) <(sleep 1; grep "^cpu " /proc/stat)
}

start_cpu_monitor() {
    (
        high_count=0
        while true; do
            cpu="$(cpu_percent_once || echo 0)"
            echo "$(date -Is),cpu_percent=${cpu}" >>"${RUN_DIR}/cpu_monitor.log"
            [[ "${cpu}" -ge "${AUTO_STOP_CPU_PERCENT}" ]] && \
                high_count=$((high_count+1)) || high_count=0
            if [[ "${high_count}" -ge 5 ]]; then
                echo "CPU >${AUTO_STOP_CPU_PERCENT}% for 5s; aborting." | \
                    tee "${ABORT_FILE}" >>"${RUN_DIR}/cpu_monitor.log"
                for pid in ${PIDS:-}; do kill "${pid}" >/dev/null 2>&1 || true; done
                exit 0
            fi
        done
    ) &
    MONITOR_PID=$!
}

"${ROOT_DIR}/scripts/network/cleanup.sh" >/dev/null 2>&1 || true
"${ROOT_DIR}/scripts/network/setup.sh" >"${RUN_DIR}/setup.log"

# Start camera services
for idx in 1 2 3 4; do
    ip netns exec "ns_camera${idx}" python3 "${ROOT_DIR}/traffic/camera_service.py" \
        --camera-id "camera${idx}" \
        --telemetry-port "${TELEMETRY_PORT}" \
        --attack-port    "${UDP_ATTACK_PORT}" \
        --service-port   "${CAMERA_SERVICE_PORT}" \
        --metrics        "${RUN_DIR}/camera${idx}.json" \
        >"${RUN_DIR}/camera${idx}.log" 2>&1 &
    PIDS+=" $!"
done
sleep 1

# Attach defense
case "${CASE}" in
    no_defense)
        echo "mode=no_defense" >"${RUN_DIR}/attach_status.txt"
        ;;
    linux_filter)
        "${ROOT_DIR}/policy/nftables/apply.sh" >"${RUN_DIR}/defense.log" 2>&1
        echo "mode=nftables" >"${RUN_DIR}/attach_status.txt"
        ;;
    cms_ebpf|xdp_cms_ebpf|elastic_ebpf|xdp_elastic_ebpf)
        # XDP is the primary attach method on Mac/Lima
        CASE="${CASE}" "${ROOT_DIR}/scripts/deployment/attach-xdp.sh" >"${RUN_DIR}/defense.log" 2>&1
        cp "${ROOT_DIR}/eval/results/${CASE}_attach_status.txt" \
           "${RUN_DIR}/attach_status.txt" 2>/dev/null || true
        ;;
    *)
        echo "Unknown CASE=${CASE}" | tee "${RUN_DIR}/error.log"; exit 1
        ;;
esac

# Start APOC telemetry client
ip netns exec "${NS_APOC}" python3 "${ROOT_DIR}/traffic/telemetry_client.py" \
    --targets       "${CAMERA_TARGETS}" \
    --port          "${TELEMETRY_PORT}" \
    --interval-ms   "${TELEMETRY_INTERVAL_MS}" \
    --duration-sec  "${TEST_DURATION_SEC}" \
    --out           "${RUN_DIR}/telemetry.json" \
    --csv-out       "${RUN_DIR}/telemetry.csv" \
    >"${RUN_DIR}/telemetry.log" 2>&1 &
TELEM_PID=$!
PIDS+=" ${TELEM_PID}"
sleep 1

# Start attack
if [[ "${ATTACK_TYPE}" == "udp" ]]; then
    ATTACK_PPS="${ATTACK_PPS:-${UDP_ATTACK_PPS}}"
    [[ "${SAFE_MODE}" == "1" && "${ATTACK_PPS}" -gt "${MAX_ATTACK_PPS}" ]] && \
        ATTACK_PPS="${MAX_ATTACK_PPS}"
    ip netns exec "${NS_ATTACKER}" python3 "${ROOT_DIR}/traffic/udp_flood.py" \
        --targets "${CAMERA_TARGETS}" --port "${UDP_ATTACK_PORT}" \
        --pps "${ATTACK_PPS}" --duration-sec "${TEST_DURATION_SEC}" \
        --mode "${TARGET_MODE}" --out "${RUN_DIR}/attack.json" \
        >"${RUN_DIR}/attack.log" 2>&1 &
elif [[ "${ATTACK_TYPE}" == "syn" ]]; then
    ATTACK_PPS="${ATTACK_PPS:-${SYN_ATTACK_PPS}}"
    [[ "${SAFE_MODE}" == "1" && "${ATTACK_PPS}" -gt "${MAX_ATTACK_PPS}" ]] && \
        ATTACK_PPS="${MAX_ATTACK_PPS}"
    ip netns exec "${NS_ATTACKER}" python3 "${ROOT_DIR}/traffic/syn_flood.py" \
        --targets "${CAMERA_TARGETS}" --port "${SYN_ATTACK_PORT}" \
        --pps "${ATTACK_PPS}" --duration-sec "${TEST_DURATION_SEC}" \
        --mode "${TARGET_MODE}" --out "${RUN_DIR}/attack.json" \
        >"${RUN_DIR}/attack.log" 2>&1 &
else
    echo "Unknown ATTACK_TYPE=${ATTACK_TYPE}" | tee "${RUN_DIR}/error.log"; exit 1
fi
ATTACK_PID=$!
PIDS+=" ${ATTACK_PID}"
start_cpu_monitor

wait "${ATTACK_PID}" || true
wait "${TELEM_PID}"  || true
[[ -n "${MONITOR_PID}" ]] && kill "${MONITOR_PID}" >/dev/null 2>&1 || true
sleep 1

# Collect TC / XDP stats from guard
ip netns exec "${NS_GUARD}" \
    tc -s filter show dev "${GUARD_TERMINAL_IF}" ingress \
    >"${RUN_DIR}/tc_stats.txt" 2>&1 || true

# Collect XDP program info via bpftool (if available)
ip netns exec "${NS_GUARD}" \
    bpftool net show dev "${GUARD_TERMINAL_IF}" \
    >"${RUN_DIR}/xdp_stats.txt" 2>&1 || true

ip netns exec "${NS_GUARD}" \
    nft list ruleset >"${RUN_DIR}/nft_ruleset.txt" 2>&1 || true

python3 "${ROOT_DIR}/eval/collect_metrics.py" \
    --run-dir "${RUN_DIR}" --case "${CASE}" --attack-type "${ATTACK_TYPE}" \
    | tee "${RUN_DIR}/collect_metrics.log"

echo "${RUN_DIR}"
