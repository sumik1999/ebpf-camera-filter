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
    ct state established,related accept
    ip saddr ${APOC_IP} accept
    ip daddr ${APOC_IP} accept
    udp dport ${UDP_ATTACK_PORT} limit rate 300/second burst 100 packets accept
    udp dport ${UDP_ATTACK_PORT} drop
    tcp dport ${SYN_ATTACK_PORT} tcp flags & (syn | ack) == syn limit rate 300/second burst 100 packets accept
    tcp dport ${SYN_ATTACK_PORT} tcp flags & (syn | ack) == syn drop
  }
}
NFT

echo "nftables guard installed in ${NS_GUARD}"
