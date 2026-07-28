#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck source=/dev/null
source "${ROOT_DIR}/config.env"

if [[ "${EUID}" -ne 0 ]]; then
  echo "Run with sudo: sudo $0"
  exit 1
fi

ip netns exec "${NS_GUARD}" nft delete table inet airport_guard >/dev/null 2>&1 || true
ip netns exec "${NS_GUARD}" nft -f - <<NFT
table inet airport_guard {
  chain forward_guard {
    type filter hook forward priority 0; policy accept;

    ct state invalid counter drop
    iifname "${GUARD_TERMINAL_IF}" ip saddr != ${TERMINAL_CIDR} counter drop
    iifname "${GUARD_CAMERA_IF}" ip saddr != ${CAMERA_CIDR} counter drop

    # APOC telemetry is trusted only on the expected ingress interface,
    # source address, camera destination prefix, and service port. It remains
    # bounded so source-address spoofing cannot create an unlimited bypass.
    iifname "${GUARD_TERMINAL_IF}" ip saddr ${APOC_IP} ip daddr ${CAMERA_CIDR} \
      udp dport ${TELEMETRY_PORT} limit rate ${APOC_TRUST_RATE_PPS}/second \
      burst ${APOC_TRUST_BURST_PACKETS} packets accept
    iifname "${GUARD_TERMINAL_IF}" ip saddr ${APOC_IP} ip daddr ${CAMERA_CIDR} \
      udp dport ${TELEMETRY_PORT} counter drop

    # Evaluate bounded trusted requests before this stateful fast path;
    # camera replies and other established traffic can then pass normally.
    ct state established,related accept

    udp dport ${UDP_ATTACK_PORT} limit rate 300/second burst 100 packets accept
    udp dport ${UDP_ATTACK_PORT} drop
    tcp dport ${SYN_ATTACK_PORT} tcp flags & (syn | ack) == syn limit rate 300/second burst 100 packets accept
    tcp dport ${SYN_ATTACK_PORT} tcp flags & (syn | ack) == syn drop
  }
}
NFT

echo "nftables guard installed in ${NS_GUARD}"
