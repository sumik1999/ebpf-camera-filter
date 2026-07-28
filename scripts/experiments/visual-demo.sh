#!/usr/bin/env bash
# run_visual_demo.sh – Mac / Lima edition.
# Runs a short no_defense + xdp_elastic_ebpf demo and generates HTML replay.

set -euo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
# shellcheck source=/dev/null
source "${ROOT_DIR}/scripts/lib/load-config.sh"

[[ "${EUID}" -ne 0 ]] && { echo "Run with sudo: sudo $0"; exit 1; }

DURATION="${VISUAL_DEMO_DURATION_SEC:-10}"
PPS="${VISUAL_DEMO_ATTACK_PPS:-2000}"
TARGET_MODE="${VISUAL_DEMO_TARGET_MODE:-single_camera}"
ATTACK_TYPE="${VISUAL_DEMO_ATTACK_TYPE:-udp}"

[[ "${SAFE_MODE}" == "1" && "${PPS}" -gt "${MAX_ATTACK_PPS}" ]] && PPS="${MAX_ATTACK_PPS}"

cpu_now() {
    awk '
      NR==1 { idle1=$5; total1=0; for(i=2;i<=NF;i++) total1+=$i }
      NR==2 { idle2=$5; total2=0; for(i=2;i<=NF;i++) total2+=$i
        dt=total2-total1; di=idle2-idle1
        if (dt>0) printf "%.0f\n",(100*(dt-di)/dt); else print 0 }
    ' <(grep "^cpu " /proc/stat) <(sleep 1; grep "^cpu " /proc/stat)
}

current_cpu="$(cpu_now || echo 0)"
if [[ "${SAFE_MODE}" == "1" && "${current_cpu}" -ge 70 ]]; then
    echo "CPU at ${current_cpu}%; reducing demo to 5s / 1000 PPS"
    DURATION=5; PPS=1000
fi

echo "Visual demo: duration=${DURATION}s pps=${PPS} mode=${TARGET_MODE} attack=${ATTACK_TYPE}"
"${ROOT_DIR}/scripts/network/cleanup.sh" >/dev/null 2>&1 || true

# Run 1: no defense baseline
CASE=no_defense ATTACK_TYPE="${ATTACK_TYPE}" TARGET_MODE="${TARGET_MODE}" \
    TEST_DURATION_SEC="${DURATION}" ATTACK_PPS="${PPS}" \
    bash "${ROOT_DIR}/scripts/experiments/run.sh"

# Run 2: XDP ElasticSketch (preferred on Mac)
echo "Preflighting xdp_elastic_ebpf …"
elastic_ok=0
"${ROOT_DIR}/scripts/network/setup.sh" >/dev/null
CASE=xdp_elastic_ebpf \
    bash "${ROOT_DIR}/scripts/deployment/attach-xdp.sh" >/tmp/airport_visual_attach.log 2>&1 || true

if grep -qE "mode=(xdp_attached|tc_fallback_attached)" \
        "${ROOT_DIR}/eval/results/xdp_elastic_ebpf_attach_status.txt" 2>/dev/null; then
    elastic_ok=1
fi
"${ROOT_DIR}/scripts/deployment/detach.sh" >/dev/null 2>&1 || true
"${ROOT_DIR}/scripts/network/cleanup.sh" >/dev/null 2>&1 || true

if [[ "${elastic_ok}" -eq 1 ]]; then
    CASE=xdp_elastic_ebpf ATTACK_TYPE="${ATTACK_TYPE}" TARGET_MODE="${TARGET_MODE}" \
        TEST_DURATION_SEC="${DURATION}" ATTACK_PPS="${PPS}" \
        bash "${ROOT_DIR}/scripts/experiments/run.sh"
else
    echo "WARNING: xdp_elastic_ebpf attach failed; skipping eBPF visual run."
    cat /tmp/airport_visual_attach.log 2>/dev/null || true
fi

python3 "${ROOT_DIR}/eval/summarize_results.py" --results-dir "${ROOT_DIR}/eval/results"
python3 "${ROOT_DIR}/eval/visual_attack_sim.py" --results-dir "${ROOT_DIR}/eval/results" \
    --run-id latest

HTML="${ROOT_DIR}/eval/results/visual_replay.html"
PNG="${ROOT_DIR}/eval/results/visual_summary.png"
echo ""
echo "Outputs:"
echo "  HTML replay: ${HTML}"
[[ -f "${PNG}" ]] && echo "  PNG summary: ${PNG}"
echo ""
echo "To view on macOS host (Lima):"
echo "  limactl copy default:${HTML} ~/Desktop/visual_replay.html"
echo "  open ~/Desktop/visual_replay.html"
