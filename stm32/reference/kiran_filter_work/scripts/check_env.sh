#!/usr/bin/env bash
# check_env.sh – Pre-flight environment audit for Mac / Lima.
# Verifies we are inside a Linux VM and all required tools are present.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck source=/dev/null
source "${ROOT_DIR}/config.env"

OK=0 WARN=0 FAIL=0
pass()  { echo "  [ OK ] $*"; OK=$((OK+1)); }
warn()  { echo "  [WARN] $*"; WARN=$((WARN+1)); }
fail()  { echo "  [FAIL] $*"; FAIL=$((FAIL+1)); }
check() { command -v "$1" >/dev/null 2>&1 && pass "$1 found" || fail "$1 not found"; }

echo "════════════════════════════════════════"
echo " Airport eBPF Mac/Lima Environment Check"
echo "════════════════════════════════════════"

echo ""
echo "── Platform ──"
if [[ "$(uname -s)" == "Linux" ]]; then
    pass "Running on Linux ($(uname -r))"
    ARCH="$(uname -m)"
    pass "Architecture: ${ARCH}"
    if grep -qi "lima\|orbstack\|docker\|wsl" /proc/version 2>/dev/null; then
        pass "VM environment detected"
    else
        warn "Cannot detect VM type; ensure this is Lima/OrbStack/Docker"
    fi
else
    fail "Not running on Linux! Open a Lima/OrbStack shell first."
    echo ""
    echo "  Lima:      limactl shell default"
    echo "  OrbStack:  orb"
    exit 1
fi

echo ""
echo "── Required tools ──"
check clang
check llvm-strip
check python3
check ip
check tc
check nft
check ping

echo ""
echo "── eBPF / XDP tools ──"
check bpftool   || warn "bpftool optional but useful for XDP inspection"
check ethtool   || warn "ethtool optional (used to disable GRO on guard NIC)"

echo ""
echo "── Python packages ──"
python3 -c "import socket"   && pass "socket" || fail "socket (stdlib)"
python3 -c "import argparse" && pass "argparse" || fail "argparse (stdlib)"
python3 -c "import json"     && pass "json" || fail "json (stdlib)"

echo ""
echo "── Kernel eBPF / XDP support ──"
if [[ -f /proc/sys/kernel/unprivileged_bpf_disabled ]]; then
    val=$(cat /proc/sys/kernel/unprivileged_bpf_disabled)
    [[ "${val}" -le 1 ]] && pass "eBPF available (unprivileged_bpf_disabled=${val})" \
        || warn "eBPF restricted (need root, which we use anyway)"
else
    warn "/proc/sys/kernel/unprivileged_bpf_disabled not found – may be fine"
fi

# Test XDP SKB mode by checking if generic XDP is compiled in
if grep -q "XDP" /boot/config-"$(uname -r)" 2>/dev/null || \
   grep -q "BPF" /boot/config-"$(uname -r)" 2>/dev/null; then
    pass "Kernel BPF/XDP config found"
else
    warn "Could not read kernel config; assuming XDP SKB mode available"
fi

echo ""
echo "── libbpf headers ──"
if [[ -f /usr/include/bpf/bpf_helpers.h ]]; then
    pass "bpf_helpers.h found at /usr/include/bpf/"
elif [[ -f /usr/local/include/bpf/bpf_helpers.h ]]; then
    pass "bpf_helpers.h found at /usr/local/include/bpf/"
else
    fail "bpf_helpers.h not found – install libbpf-dev:  sudo apt install libbpf-dev"
fi

echo ""
echo "── Linux headers ──"
KERN="$(uname -r)"
if [[ -d /usr/src/linux-headers-"${KERN}" ]]; then
    pass "linux-headers-${KERN} found"
elif [[ -d /usr/include/linux ]]; then
    pass "/usr/include/linux found (sufficient for XDP compile)"
else
    fail "Linux headers missing – sudo apt install linux-headers-${KERN} linux-libc-dev"
fi

echo ""
echo "════════════════════════════════════════"
echo " Results: ${OK} OK  |  ${WARN} warnings  |  ${FAIL} failures"
echo "════════════════════════════════════════"

if [[ "${FAIL}" -gt 0 ]]; then
    echo ""
    echo "Fix the failures above, then re-run: sudo bash scripts/check_env.sh"
    echo ""
    echo "Quick install for Ubuntu Lima:"
    echo "  sudo apt update && sudo apt install -y clang llvm libbpf-dev \\"
    echo "    linux-headers-\$(uname -r) iproute2 nftables bridge-utils \\"
    echo "    python3 bpftool ethtool"
    exit 1
fi
echo ""
echo "Environment OK – run: sudo make setup && sudo make stress-matrix"
