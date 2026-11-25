#!/usr/bin/env bash
# Attach the TC/eBPF T-or-N dataplane, then run the topology controller.
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
[[ "${EUID}" -eq 0 ]] || { echo "Run as root"; exit 1; }
bash "${ROOT_DIR}/scripts/attach_rapid_dataplane.sh"
exec bash "${ROOT_DIR}/scripts/attach_rapid_adaptive.sh" "$@"
