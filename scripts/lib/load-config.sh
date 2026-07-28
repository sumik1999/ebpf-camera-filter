#!/usr/bin/env bash
# Load public project defaults, followed by an optional machine-local override.
# The caller must define ROOT_DIR before sourcing this file.

if [[ -z "${ROOT_DIR:-}" ]]; then
    echo "load-config.sh: ROOT_DIR must be set by the calling script" >&2
    return 1
fi

# shellcheck source=/dev/null
source "${ROOT_DIR}/config/defaults.env"

LOCAL_CONFIG="${AIRPORT_CONFIG:-${ROOT_DIR}/config/local.env}"
if [[ -f "${LOCAL_CONFIG}" ]]; then
    # shellcheck source=/dev/null
    source "${LOCAL_CONFIG}"
fi
