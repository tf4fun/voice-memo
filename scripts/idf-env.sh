#!/usr/bin/env bash
# Activate the repo-local ESP-IDF 5.5.3 environment in the current shell.
#
#   source bin/idf-env.sh
#
# Deliberately not executable-by-./ : export.sh must be sourced, not spawned.
set -euo pipefail

WORKSPACE_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
export IDF_TOOLS_PATH="${WORKSPACE_ROOT}/bin/espressif"
export IDF_PATH="${WORKSPACE_ROOT}/bin/esp-idf-v5.5.3"
# The Component Manager otherwise uses ~/Library/Caches/Espressif/ComponentManager,
# which is outside this workspace and therefore not writable under the file sandbox.
export IDF_COMPONENT_CACHE_PATH="${WORKSPACE_ROOT}/bin/espressif/component_cache"

if [ ! -f "${IDF_PATH}/export.sh" ]; then
    echo "ESP-IDF checkout missing at ${IDF_PATH}; run bin/setup-idf.sh first" >&2
    return 1 2>/dev/null || exit 1
fi

# Keep the user's shell strictness; export.sh is not -u clean.
set +u
# shellcheck disable=SC1091
. "${IDF_PATH}/export.sh" >/dev/null
set -u
echo "ESP-IDF activated: $(idf.py --version 2>/dev/null | tail -1)"
