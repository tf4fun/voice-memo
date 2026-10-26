#!/usr/bin/env bash
# Repo-local ESP-IDF 5.5.3 installer for FoloToy AI Passport development.
#
# Keeps every downloaded byte inside this workspace:
#   bin/esp-idf-v5.5.3   ESP-IDF source checkout (tag v5.5.3)
#   bin/espressif        IDF_TOOLS_PATH (toolchains, python venv, dist cache)
#
# No sudo, no Homebrew install, no writes outside the workspace.
# Re-runnable: repairs a partial submodule fetch instead of re-cloning.
set -euo pipefail

WORKSPACE_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
BIN_DIR="${WORKSPACE_ROOT}/bin"
IDF_TAG="${IDF_TAG:-v5.5.3}"
IDF_DIR="${IDF_DIR:-${BIN_DIR}/esp-idf-${IDF_TAG}}"
export IDF_TOOLS_PATH="${IDF_TOOLS_PATH:-${BIN_DIR}/espressif}"
# Keep the Component Manager cache inside the workspace as well; its default
# (~/Library/Caches/Espressif/ComponentManager on macOS) is outside the sandbox.
export IDF_COMPONENT_CACHE_PATH="${IDF_COMPONENT_CACHE_PATH:-${IDF_TOOLS_PATH}/component_cache}"
TARGET="${1:-esp32c3}"

mkdir -p "${IDF_TOOLS_PATH}"
echo "== ESP-IDF checkout: ${IDF_DIR}"
echo "== IDF_TOOLS_PATH:   ${IDF_TOOLS_PATH}"

if [ ! -d "${IDF_DIR}/.git" ]; then
    echo "== cloning esp-idf ${IDF_TAG} (recursive) ..."
    git clone --branch "${IDF_TAG}" --recursive \
        https://github.com/espressif/esp-idf.git "${IDF_DIR}"
else
    echo "== existing checkout found; repairing submodules ..."
    git -C "${IDF_DIR}" submodule update --init --recursive
fi

echo "== checkout version:"
git -C "${IDF_DIR}" describe --tags 2>/dev/null || echo "(no tag)"

echo "== checkout state:"
git -C "${IDF_DIR}" status --short --branch | head -20

echo "== installing ${TARGET} toolchain ..."
"${IDF_DIR}/install.sh" "${TARGET}"

echo "== install complete; verifying activation"
set +u
# shellcheck disable=SC1091
source "${IDF_DIR}/export.sh" >/dev/null 2>&1 || true
set -u
if command -v idf.py >/dev/null 2>&1; then
    idf.py --version
else
    echo "WARNING: idf.py not resolvable after export.sh" >&2
    exit 1
fi
