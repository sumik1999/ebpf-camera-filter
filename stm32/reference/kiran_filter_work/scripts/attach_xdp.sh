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
#   3. Attaches the object to the guard gateway interface using
#      "ip link set … xdp obj … sec xdp" in the chosen mode.
#   4. Falls back to TC ingress (attach_tc_fallback.sh) if XDP attachment
#      fails (e.g. on veth pairs without SKB mode support).
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

# ── Map CASE aliases to source files ─────────────────────────────────────
case "${CASE}" in
    xdp_cms_ebpf|cms_ebpf)
        SRC="${ROOT_DIR}/ebpf/xdp_cms_filter.bpf.c"
        OBJ="${ROOT_DIR}/ebpf/xdp_cms_filter.bpf.o"
        CASE_CANON="xdp_cms_ebpf"
        ;;
    xdp_elastic_ebpf|elastic_ebpf)
        SRC="${ROOT_DIR}/ebpf/xdp_elastic_filter.bpf.c"
        OBJ="${ROOT_DIR}/ebpf/xdp_elastic_filter.bpf.o"
        CASE_CANON="xdp_elastic_ebpf"
        ;;
    *)
        echo "Unsupported XDP CASE: ${CASE}" | tee "${STATUS_FILE}"
        echo "Valid values: xdp_cms_ebpf, xdp_elastic_ebpf, cms_ebpf, elastic_ebpf"
        exit 1
        ;;
esac

echo "case=${CASE_CANON}"     >"${STATUS_FILE}"
echo "source=${SRC}"          >>"${STATUS_FILE}"
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
    echo "       Falling back to TC ingress …"
    CASE="${CASE_CANON}" bash "${ROOT_DIR}/scripts/attach_tc_fallback.sh"
    exit 0
fi
echo "Compile OK: ${OBJ}" | tee -a "${STATUS_FILE}"

# ── Attach XDP program ───────────────────────────────────────────────────
XDP_MODE_FLAG=""
case "${XDP_MODE:-skb}" in
    native)  XDP_MODE_FLAG=""        ;;   # driver-level, no flag needed
    skb)     XDP_MODE_FLAG="generic" ;;   # SKB/generic for veth / virtio
    offload) XDP_MODE_FLAG="offload" ;;   # NIC offload (rare)
esac

# Detach any existing XDP program first
ip netns exec "${NS_GUARD}" \
    ip link set dev "${GUARD_TERMINAL_IF}" xdp off 2>/dev/null || true

ATTACH_CMD="ip link set dev ${GUARD_TERMINAL_IF} xdp${XDP_MODE_FLAG:+ ${XDP_MODE_FLAG}} obj ${OBJ} sec xdp"
echo "Attaching: ${ATTACH_CMD}" | tee -a "${STATUS_FILE}"

if ip netns exec "${NS_GUARD}" ${ATTACH_CMD} >>"${STATUS_FILE}" 2>&1; then
    echo "mode=xdp_attached_${XDP_MODE:-skb}" >>"${STATUS_FILE}"
    echo "SUCCESS: XDP ${CASE_CANON} attached on ${NS_GUARD}/${GUARD_TERMINAL_IF} (mode=${XDP_MODE:-skb})"
else
    echo "mode=xdp_attach_failed" >>"${STATUS_FILE}"
    echo "WARNING: XDP attach failed (mode=${XDP_MODE:-skb}). Falling back to TC …"
    CASE="${CASE_CANON}" bash "${ROOT_DIR}/scripts/attach_tc_fallback.sh"
fi
