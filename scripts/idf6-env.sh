#!/usr/bin/env bash
# Activate the repo-local ESP-IDF v6.1 environment in the current shell.
#
#   source bin/idf6-env.sh
#
# Used by the XiaoZhi-based firmware (FoloToy/folo-ai-passport-xiaozhi), which
# requires ESP-IDF >= 6.0.1 and does not build with 5.x. The ai-passport
# baseline in this workspace uses bin/idf-env.sh (5.5.3) instead; the two
# installations are deliberately side by side with separate IDF_TOOLS_PATHs.
set -euo pipefail

WORKSPACE_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
export IDF_TOOLS_PATH="${WORKSPACE_ROOT}/bin/espressif-6"
export IDF_PATH="${WORKSPACE_ROOT}/bin/esp-idf-v6.1"
export IDF_COMPONENT_CACHE_PATH="${WORKSPACE_ROOT}/bin/espressif-6/component_cache"
# ESP-IDF 6.x needs Python >= 3.10; the system python3 is 3.9, so use the
# standalone CPython shipped in bin/python.
export PATH="${WORKSPACE_ROOT}/bin/python/bin:${PATH}"

if [ ! -f "${IDF_PATH}/export.sh" ]; then
    echo "ESP-IDF v6.1 checkout missing at ${IDF_PATH}; run:" >&2
    echo "  IDF_TAG=v6.1 IDF_TOOLS_PATH=${IDF_TOOLS_PATH} bin/setup-idf.sh esp32c3" >&2
    return 1 2>/dev/null || exit 1
fi

set +u
# shellcheck disable=SC1091
. "${IDF_PATH}/export.sh" >/dev/null
set -u
echo "ESP-IDF activated: $(idf.py --version 2>/dev/null | tail -1)"
