#!/usr/bin/env bash
# Run the repository static gate on Linux through Docker.
#
# The gate supports native macOS and Linux linkers. This optional wrapper runs
# the same host suites using Linux/GCC to reproduce the CI environment.
#
#   ./scripts/static-gate-linux.sh
#
# The checkout is bind-mounted read-only for the source, but the gate writes host
# test binaries into a temporary directory, so the whole tree stays writable.
set -euo pipefail

REPO_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"

if ! command -v docker >/dev/null 2>&1; then
    echo "docker is required for the Linux static gate" >&2
    exit 1
fi

exec docker run --rm -v "${REPO_ROOT}":/project -w /project debian:bookworm-slim \
    bash -lc 'apt-get update -qq >/dev/null 2>&1 &&
              apt-get install -y -qq build-essential python3 git curl ca-certificates tar >/dev/null 2>&1 &&
              ./tools/validate.sh --static'
