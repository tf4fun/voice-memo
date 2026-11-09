#!/usr/bin/env bash
# Build and run the LAN server using local, literal environment configuration.
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
env_file="${MEMO_ENV_FILE:-${repo_root}/server/.env.local}"
if [[ ! -r "${env_file}" ]]; then
    echo "Create server/.env.local from server/.env.example and set MEMO_ASR_API_KEY." >&2
    exit 1
fi

# Read values as data, never source/eval a file containing a credential. Optional
# matching quotes are literal delimiters; shell expansion is deliberately absent.
line_number=0
while IFS= read -r line || [[ -n "${line}" ]]; do
    line_number=$((line_number + 1))
    line="${line%$'\r'}"
    [[ "${line}" =~ ^[[:space:]]*(#|$) ]] && continue
    if [[ ! "${line}" =~ ^(MEMO_[A-Z0-9_]+)=(.*)$ ]]; then
        echo "Invalid configuration at line ${line_number}; expected MEMO_NAME=value." >&2
        exit 1
    fi
    key="${BASH_REMATCH[1]}"
    value="${BASH_REMATCH[2]}"
    case "${key}" in
        MEMO_ADDR|MEMO_DATA_DIR|MEMO_MDNS_NAME|MEMO_MDNS_INTERFACE|MEMO_ASR_PROVIDER|MEMO_ASR_API_KEY|MEMO_ASR_MODEL|MEMO_ASR_BASE_URL|MEMO_ASR_CONCURRENCY|MEMO_MAX_BYTES|MEMO_MAX_MEMO_MS|MEMO_RETENTION_DAYS|MEMO_LOG_LEVEL) ;;
        *) echo "Unknown server setting at line ${line_number}." >&2; exit 1 ;;
    esac
    if [[ "${value}" == \"*\" || "${value}" == \'*\' ]]; then
        value="${value:1:${#value}-2}"
    fi
    export "${key}=${value}"
done < "${env_file}"

if [[ "${1:-}" == --check-env ]]; then
    echo "Server environment syntax: PASS"
    exit 0
fi

# Reuse the repository's Go cache setup and retain the binary outside source.
source "${repo_root}/scripts/go-env.sh"
mkdir -p "${repo_root}/build/server"
cd "${repo_root}/server"
CGO_ENABLED=0 go build -trimpath -ldflags="-s -w" \
    -o "${repo_root}/build/server/memo-server" ./cmd/memo-server
exec "${repo_root}/build/server/memo-server" "$@"
