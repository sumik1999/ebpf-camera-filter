#!/usr/bin/env bash
# attach_tc_fallback.sh – TC-ingress fallback when XDP attach fails.
#
# Called automatically by attach_xdp.sh if XDP mode is unavailable.
# Also usable standalone for debugging:
#   sudo CASE=xdp_elastic_ebpf bash scripts/attach_tc_fallback.sh
#
# Mac / Lima note: on veth pairs inside Linux network namespaces, XDP
# generic (skb) mode should work, but some older Lima kernels or
# unusual configurations may not support it. TC ingress is always
# available on veth and acts as a reliable fallback.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck source=/dev/null
source "${ROOT_DIR}/config.env"

CASE="${CASE:-${1:-xdp_elastic_ebpf}}"
STATUS_DIR="${ROOT_DIR}/eval/results"
mkdir -p "${STATUS_DIR}"
STATUS_FILE="${STATUS_DIR}/${CASE}_attach_status.txt"

if [[ "${EUID}" -ne 0 ]]; then
    echo "Run with sudo: sudo CASE=${CASE} $0"; exit 1
fi

# Map XDP case names to TC-compatible source files
case "${CASE}" in
    xdp_cms_ebpf|cms_ebpf)
        SRC="${ROOT_DIR}/ebpf/xdp_cms_filter.bpf.c"
        OBJ="${ROOT_DIR}/ebpf/xdp_cms_filter_tc.bpf.o"
        ;;
    xdp_elastic_ebpf|elastic_ebpf)
        SRC="${ROOT_DIR}/ebpf/xdp_elastic_filter.bpf.c"
        OBJ="${ROOT_DIR}/ebpf/xdp_elastic_filter_tc.bpf.o"
        ;;
    *) echo "Unsupported TC fallback CASE: ${CASE}"; exit 1 ;;
esac

# Detect arch
MACHINE="$(uname -m)"
case "${MACHINE}" in
    x86_64)        BPF_TARGET_ARCH="x86"   ;;
    aarch64|arm64) BPF_TARGET_ARCH="arm64" ;;
    *)             BPF_TARGET_ARCH="x86"   ;;
esac

# Compile with TC section name override (-DTC_FALLBACK inserts a
# compatibility section alias so the same source works for both XDP
# and TC; the XDP source files include a SEC("tc") alias guard below.)
#
# We rely on the kernel accepting "xdp" section programs loaded via
# tc; alternatively compile with -DUSE_TC_HOOK to rename the section.
TRIPLE="x86_64-linux-gnu"
[[ "${BPF_TARGET_ARCH}" == "arm64" ]] && TRIPLE="aarch64-linux-gnu"

SYS_INC="-I/usr/include/${TRIPLE} -I/usr/include"
LIBBPF_INC=""
[[ -d /usr/include/bpf ]] && LIBBPF_INC="-I/usr/include"

clang -O2 -g -target bpf \
    -D__TARGET_ARCH_${BPF_TARGET_ARCH} \
    -DTC_FALLBACK=1 \
    ${SYS_INC} ${LIBBPF_INC} \
    -I"${ROOT_DIR}/ebpf" \
    -c "${SRC}" -o "${OBJ}" >>"${STATUS_FILE}" 2>&1 || {
    echo "mode=tc_fallback_compile_failed" >>"${STATUS_FILE}"
    echo "ERROR: TC fallback compile failed. See ${STATUS_FILE}."; exit 1
}

ip netns exec "${NS_GUARD}" tc qdisc replace dev "${GUARD_TERMINAL_IF}" clsact
if ip netns exec "${NS_GUARD}" \
        tc filter replace dev "${GUARD_TERMINAL_IF}" ingress \
        bpf da obj "${OBJ}" sec xdp >>"${STATUS_FILE}" 2>&1; then
    echo "mode=tc_fallback_attached" >>"${STATUS_FILE}"
    echo "TC fallback ${CASE} attached on ${NS_GUARD}/${GUARD_TERMINAL_IF}"
else
    echo "mode=tc_fallback_failed" >>"${STATUS_FILE}"
    echo "ERROR: TC fallback attach also failed. Check ${STATUS_FILE}."
    exit 1
fi
