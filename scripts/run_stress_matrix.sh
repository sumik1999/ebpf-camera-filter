#!/usr/bin/env bash
# run_stress_matrix.sh – Mac / Lima edition.
# Runs all four defense cases at two attack intensities.
# XDP filters are preferred; TC fallback is tried automatically.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
USER_TEST_DURATION_SEC="${TEST_DURATION_SEC:-}"
# shellcheck source=/dev/null
source "${ROOT_DIR}/config.env"

[[ "${EUID}" -ne 0 ]] && { echo "Run with sudo: sudo $0"; exit 1; }

TEST_DURATION_SEC="${USER_TEST_DURATION_SEC:-10}"
SAFE_MODE="${SAFE_MODE:-1}"
ATTACK_TYPE="${ATTACK_TYPE:-udp}"
MATRIX_LOG="${ROOT_DIR}/eval/results/stress_matrix_$(date +%Y%m%d_%H%M%S).log"
mkdir -p "${ROOT_DIR}/eval/results"

[[ "${ATTACK_TYPE}" != "udp" ]] && {
    echo "Matrix is UDP-only. Set ATTACK_TYPE=udp." | tee "${MATRIX_LOG}"; exit 1
}

# ── Preflight: verify XDP (or TC fallback) attaches ─────────────────────
preflight_xdp() {
    local case_name="$1"
    "${ROOT_DIR}/scripts/setup_ns_tree.sh" >/dev/null
    CASE="${case_name}" "${ROOT_DIR}/scripts/attach_xdp.sh" \
        >/tmp/airport_stress_attach.log 2>&1 || true
    local status_file="${ROOT_DIR}/eval/results/${case_name}_attach_status.txt"
    local ok=1
    if grep -qE "mode=(xdp_attached|tc_fallback_attached|tc_ebpf_attached)" \
            "${status_file}" 2>/dev/null; then
        ok=0
    fi
    "${ROOT_DIR}/scripts/detach_xdp.sh" >/dev/null 2>&1 || true
    "${ROOT_DIR}/scripts/cleanup_ns.sh" >/dev/null 2>&1 || true
    return "${ok}"
}

echo "Stress matrix started $(date -Is)" | tee "${MATRIX_LOG}"
echo "SAFE_MODE=${SAFE_MODE} TEST_DURATION_SEC=${TEST_DURATION_SEC}" \
     "MAX_ATTACK_PPS=${MAX_ATTACK_PPS}" | tee -a "${MATRIX_LOG}"

CMS_OK=0 ELASTIC_OK=0
preflight_xdp xdp_cms_ebpf     && CMS_OK=1     || \
    echo "WARNING: xdp_cms_ebpf attach failed; skipping."     | tee -a "${MATRIX_LOG}"
preflight_xdp xdp_elastic_ebpf && ELASTIC_OK=1 || \
    echo "WARNING: xdp_elastic_ebpf attach failed; skipping." | tee -a "${MATRIX_LOG}"

# ── Test matrix: (label, pps, target_mode) ──────────────────────────────
settings=(
    "light 1000 round_robin"
    "concentrated 3000 single_camera"
    "heavy-safe 8000 single_camera"
)

for setting in "${settings[@]}"; do
    read -r label pps mode <<< "${setting}"
    [[ "${SAFE_MODE}" == "1" && "${pps}" -gt "${MAX_ATTACK_PPS}" ]] && \
        pps="${MAX_ATTACK_PPS}"

    for case_name in no_defense linux_filter xdp_cms_ebpf xdp_elastic_ebpf; do
        # Skip eBPF cases if preflight failed
        [[ "${case_name}" == "xdp_cms_ebpf"     && "${CMS_OK}"     -eq 0 ]] && continue
        [[ "${case_name}" == "xdp_elastic_ebpf" && "${ELASTIC_OK}" -eq 0 ]] && continue

        echo "" | tee -a "${MATRIX_LOG}"
        echo "=== CASE=${case_name} ATTACK_PPS=${pps} TARGET=${mode} (${label}) ===" \
            | tee -a "${MATRIX_LOG}"

        RUN_OUT=$(
            TEST_DURATION_SEC="${TEST_DURATION_SEC}" \
            CASE="${case_name}" \
            ATTACK_TYPE="${ATTACK_TYPE}" \
            TARGET_MODE="${mode}" \
            ATTACK_PPS="${pps}" \
            bash "${ROOT_DIR}/scripts/run_experiment.sh" 2>&1 | tee -a "${MATRIX_LOG}" | tail -1
        )
        echo "Result dir: ${RUN_OUT}" | tee -a "${MATRIX_LOG}"
    done
done

echo "" | tee -a "${MATRIX_LOG}"
echo "=== Stress matrix complete $(date -Is) ===" | tee -a "${MATRIX_LOG}"
python3 "${ROOT_DIR}/eval/summarize_results.py" \
    --results-dir "${ROOT_DIR}/eval/results" | tee -a "${MATRIX_LOG}"
