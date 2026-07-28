#!/usr/bin/env bash
# attach_xdp.sh – Compile and attach an XDP eBPF filter on the guard gateway.
#
# Mac / Lima adaptation
# ─────────────────────
# On the original WSL2 build, XDP was "optional" because WSL2's kernel
# sometimes lacks driver-level XDP support on virtual Ethernet pairs.
# On a Lima or OrbStack Linux VM the kernel is a full upstream build with
# XDP generic (SKB) mode always available, and native mode available on
# virtio-net adapters.
#
# This script:
#   1. Auto-detects the host CPU architecture (x86_64 / aarch64) and sets
#      the correct clang BPF target flag.
#   2. Compiles the selected eBPF C source with clang -target bpf.
#   3. Attaches the validation front end to the guard gateway interface.
#   4. Always attaches the selected CMS/Elastic analytics stage at TC ingress.
#      If XDP is unavailable, TC remains active as an explicitly reported fallback.
#
# Usage
#   sudo CASE=xdp_elastic_ebpf bash scripts/attach_xdp.sh
#   sudo CASE=xdp_cms_ebpf    bash scripts/attach_xdp.sh
#   sudo CASE=elastic_ebpf    bash scripts/attach_xdp.sh   # alias
#   sudo CASE=cms_ebpf        bash scripts/attach_xdp.sh   # alias

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck source=/dev/null
source "${ROOT_DIR}/config.env"

CASE="${CASE:-${1:-xdp_elastic_ebpf}}"
STATUS_DIR="${ROOT_DIR}/eval/results"
mkdir -p "${STATUS_DIR}"
STATUS_FILE="${STATUS_DIR}/${CASE}_attach_status.txt"

if [[ "${EUID}" -ne 0 ]]; then
    echo "Run with sudo: sudo CASE=${CASE} $0"
    exit 1
fi

# ── Select the TC analytics stage ────────────────────────────────────────
# Every defended case uses the same cheap XDP validation front end. CMS or
# ElasticSketch-inspired accounting then runs as a genuine TC classifier.
SRC="${ROOT_DIR}/ebpf/xdp_frontend.bpf.c"
OBJ="${ROOT_DIR}/ebpf/xdp_frontend.bpf.o"
case "${CASE}" in
    xdp_cms_ebpf|cms_ebpf)
        CASE_CANON="xdp_cms_ebpf"
        TC_VARIANT="cms"
        ;;
    xdp_elastic_ebpf|elastic_ebpf)
        CASE_CANON="xdp_elastic_ebpf"
        TC_VARIANT="elastic"
        ;;
    *)
        echo "Unsupported XDP CASE: ${CASE}" | tee "${STATUS_FILE}"
        echo "Valid values: xdp_cms_ebpf, xdp_elastic_ebpf, cms_ebpf, elastic_ebpf"
        exit 1
        ;;
esac

echo "case=${CASE_CANON}"     >"${STATUS_FILE}"
echo "pipeline=xdp_frontend+tc_${TC_VARIANT}" >>"${STATUS_FILE}"
echo "xdp_source=${SRC}"      >>"${STATUS_FILE}"
echo "interface=${NS_GUARD}/${GUARD_TERMINAL_IF}" >>"${STATUS_FILE}"

# ── Auto-detect CPU architecture for clang target flag ──────────────────
if [[ -z "${BPF_TARGET_ARCH}" ]]; then
    MACHINE="$(uname -m)"
    case "${MACHINE}" in
        x86_64)          BPF_TARGET_ARCH="x86"     ;;
        aarch64|arm64)   BPF_TARGET_ARCH="arm64"   ;;
        *)               BPF_TARGET_ARCH="x86"     ;;  # safe default
    esac
fi
echo "bpf_target_arch=${BPF_TARGET_ARCH}" >>"${STATUS_FILE}"

# ── Locate system headers ────────────────────────────────────────────────
# Lima Ubuntu: /usr/include/<triple>/
TRIPLE=""
if [[ "${BPF_TARGET_ARCH}" == "arm64" ]]; then
    TRIPLE="aarch64-linux-gnu"
else
    TRIPLE="x86_64-linux-gnu"
fi

SYS_INCLUDES=""
for candidate in "/usr/include/${TRIPLE}" "/usr/include" "/usr/local/include"; do
    if [[ -d "${candidate}" ]]; then
        SYS_INCLUDES="${SYS_INCLUDES} -I${candidate}"
    fi
done

# bpf/bpf_helpers.h may live in /usr/include/bpf or from libbpf-dev
LIBBPF_INCLUDES=""
for candidate in "/usr/include/bpf" "/usr/local/include/bpf"; do
    if [[ -d "${candidate}" ]]; then
        LIBBPF_INCLUDES="-I${candidate%/bpf}"
        break
    fi
done

# ── Compile ──────────────────────────────────────────────────────────────
echo "Compiling ${SRC} …" | tee -a "${STATUS_FILE}"
# shellcheck disable=SC2086
if ! clang -O2 -g \
        -target bpf \
        -D__TARGET_ARCH_${BPF_TARGET_ARCH} \
        ${SYS_INCLUDES} \
        ${LIBBPF_INCLUDES} \
        -I"${ROOT_DIR}/ebpf" \
        -c "${SRC}" -o "${OBJ}" >>"${STATUS_FILE}" 2>&1; then
    echo "mode=xdp_compile_failed" >>"${STATUS_FILE}"
    echo "ERROR: XDP eBPF compile failed. See ${STATUS_FILE} for details."
    echo "       Continuing with TC-only fallback …"
    TC_ROLE=fallback CASE="${CASE}" bash "${ROOT_DIR}/scripts/attach_tc_fallback.sh"
    exit 0
fi
echo "Compile OK: ${OBJ}" | tee -a "${STATUS_FILE}"

# ── Attach XDP program ───────────────────────────────────────────────────
case "${XDP_MODE:-skb}" in
    native)  XDP_ATTACH_TOKEN="xdpdrv"     ;;
    skb)     XDP_ATTACH_TOKEN="xdpgeneric" ;;
    offload) XDP_ATTACH_TOKEN="xdpoffload" ;;
    *)
        echo "Unsupported XDP_MODE=${XDP_MODE}; use native, skb, or offload" | tee -a "${STATUS_FILE}"
        exit 1
        ;;
esac

# Detach any existing XDP program first.
ip netns exec "${NS_GUARD}" \
    ip link set dev "${GUARD_TERMINAL_IF}" xdp off 2>/dev/null || true

# bpftool pins the blocklist maps so updates survive program reloads and can be
# managed by scripts/blocklistctl.py. If bpftool is unavailable, retain packet
# validation but clearly report that dynamic blocklist management is disabled.
BPF_PIN_DIR="${AIRPORT_BPF_PIN_DIR:-/sys/fs/bpf/airport-defense}"
PROGRAM_PIN="${BPF_PIN_DIR}/xdp_frontend"
MAP_NAMES=(exact_deny prefix_deny bloom_bits defense_cfg)
XDP_ATTACHED=0

if command -v bpftool >/dev/null 2>&1; then
    mountpoint -q /sys/fs/bpf || mount -t bpf bpf /sys/fs/bpf
    mkdir -p "${BPF_PIN_DIR}"
    chmod 0700 "${BPF_PIN_DIR}"
    rm -f "${PROGRAM_PIN}"

    MAPS_COMPLETE=1
    for map_name in "${MAP_NAMES[@]}"; do
        [[ -e "${BPF_PIN_DIR}/${map_name}" ]] || MAPS_COMPLETE=0
    done

    LOAD_CMD=(bpftool prog load "${OBJ}" "${PROGRAM_PIN}" type xdp)
    if [[ "${MAPS_COMPLETE}" == 1 ]]; then
        for map_name in "${MAP_NAMES[@]}"; do
            LOAD_CMD+=(map name "${map_name}" pinned "${BPF_PIN_DIR}/${map_name}")
        done
    else
        for map_name in "${MAP_NAMES[@]}"; do
            rm -f "${BPF_PIN_DIR}/${map_name}"
        done
        LOAD_CMD+=(pinmaps "${BPF_PIN_DIR}")
    fi

    if "${LOAD_CMD[@]}" >>"${STATUS_FILE}" 2>&1 &&
       ip netns exec "${NS_GUARD}" \
           ip link set dev "${GUARD_TERMINAL_IF}" "${XDP_ATTACH_TOKEN}" \
           pinned "${PROGRAM_PIN}" >>"${STATUS_FILE}" 2>&1; then
        XDP_ATTACHED=1
        echo "blocklist_maps=${BPF_PIN_DIR}" >>"${STATUS_FILE}"
    fi
else
    echo "WARNING: bpftool unavailable; blocklist maps will not be pinned" | tee -a "${STATUS_FILE}"
    if ip netns exec "${NS_GUARD}" \
        ip link set dev "${GUARD_TERMINAL_IF}" "${XDP_ATTACH_TOKEN}" \
        obj "${OBJ}" sec xdp >>"${STATUS_FILE}" 2>&1; then
        XDP_ATTACHED=1
        echo "blocklist_maps=unavailable" >>"${STATUS_FILE}"
    fi
fi

echo "Attaching XDP front end on ${NS_GUARD}/${GUARD_TERMINAL_IF}" | tee -a "${STATUS_FILE}"

if [[ "${XDP_ATTACHED}" == 1 ]]; then
    echo "xdp_mode=attached_${XDP_MODE:-skb}" >>"${STATUS_FILE}"
    echo "SUCCESS: XDP front end attached on ${NS_GUARD}/${GUARD_TERMINAL_IF} (mode=${XDP_MODE:-skb})"
    if TC_ROLE=pipeline CASE="${CASE}" bash "${ROOT_DIR}/scripts/attach_tc_fallback.sh"; then
        echo "mode=xdp_tc_${TC_VARIANT}_pipeline" >>"${STATUS_FILE}"
        echo "SUCCESS: full XDP -> TC ${TC_VARIANT} pipeline is active"
    else
        echo "mode=tc_pipeline_attach_failed" >>"${STATUS_FILE}"
        ip netns exec "${NS_GUARD}" \
            ip link set dev "${GUARD_TERMINAL_IF}" xdp off 2>/dev/null || true
        exit 1
    fi
else
    echo "xdp_mode=attach_failed" >>"${STATUS_FILE}"
    echo "WARNING: XDP front-end attach failed (mode=${XDP_MODE:-skb}). Using TC only …"
    TC_ROLE=fallback CASE="${CASE}" bash "${ROOT_DIR}/scripts/attach_tc_fallback.sh"
fi
