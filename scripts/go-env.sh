#!/usr/bin/env bash
# Put the Go build and module caches inside the workspace.
#
#   source scripts/go-env.sh
#
# The defaults (~/go/pkg/mod and ~/Library/Caches/go-build) are outside this
# workspace, which the file sandbox does not allow us to write to. Keeping the
# caches next to the toolchain also makes `rm -rf bin/go` a complete clean.
set -euo pipefail

WORKSPACE_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"

# GOPATH also holds the checksum database; leaving it at ~/go would make the
# module proxy's signature verification fail for lack of write access.
export GOPATH="${WORKSPACE_ROOT}/bin/go"
export GOMODCACHE="${GOPATH}/mod"
export GOCACHE="${GOPATH}/cache"
mkdir -p "${GOMODCACHE}" "${GOCACHE}"
