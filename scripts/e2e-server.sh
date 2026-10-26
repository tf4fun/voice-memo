#!/usr/bin/env bash
# End-to-end check of the server against the committed container fixture.
#
#   ./scripts/e2e-server.sh
#
# Starts a real binary with the fake transcriber on a temporary data directory,
# then exercises upload, listing, transcription, editing, range playback and
# deletion over HTTP. No API key, no hardware, no network beyond loopback.
set -euo pipefail

REPO_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
PORT="${E2E_PORT:-18099}"
BASE="http://127.0.0.1:${PORT}/api/v1"
FIXTURE="${REPO_ROOT}/server/internal/audio/testdata/tone-1s.memo"
MEMO_ID="00000000-0000-4000-8000-000000000000"
DEVICE_ID="E2E0E2E0E2E0"

WORK_DIR="$(mktemp -d /tmp/memo-e2e.XXXXXX)"
SERVER_PID=""

cleanup() {
    if [[ -n "${SERVER_PID}" ]] && kill -0 "${SERVER_PID}" 2>/dev/null; then
        kill "${SERVER_PID}" 2>/dev/null || true
        wait "${SERVER_PID}" 2>/dev/null || true
    fi
    rm -rf "${WORK_DIR}"
}
trap cleanup EXIT

fail() {
    echo "FAIL: $*" >&2
    exit 1
}

status_of() { # method path [body] [extra curl args...]
    local method="$1" path="$2" body="${3:-}"
    shift 3 || true
    if [[ -n "${body}" ]]; then
        curl -sS -o "${WORK_DIR}/body" -w '%{http_code}' -X "${method}" "${BASE}${path}" "$@" --data-binary "${body}"
    else
        curl -sS -o "${WORK_DIR}/body" -w '%{http_code}' -X "${method}" "${BASE}${path}" "$@"
    fi
}

json_field() { # dotted path, read from ${WORK_DIR}/body
    python3 - "${WORK_DIR}/body" "$1" <<'PY'
import json, sys
path = sys.argv[2].split(".")
with open(sys.argv[1], encoding="utf-8") as fh:
    value = json.load(fh)
for part in path:
    if isinstance(value, list):
        value = value[int(part)]
    else:
        value = value[part]
print(value if value is not None else "")
PY
}

echo "== building"
(cd "${REPO_ROOT}/server" && CGO_ENABLED=0 go build -trimpath -ldflags="-s -w" \
    -o "${WORK_DIR}/memo-server" ./cmd/memo-server)

echo "== starting on port ${PORT}"
MEMO_ASR_PROVIDER=fake \
MEMO_DATA_DIR="${WORK_DIR}/data" \
MEMO_ADDR="127.0.0.1:${PORT}" \
MEMO_LOG_LEVEL=warn \
    "${WORK_DIR}/memo-server" serve >"${WORK_DIR}/server.log" 2>&1 &
SERVER_PID=$!

for _ in $(seq 1 50); do
    if curl -sS -o /dev/null "${BASE}/ping" 2>/dev/null; then break; fi
    sleep 0.1
done
curl -sS "${BASE}/ping" >/dev/null || fail "server did not come up"

echo "== upload (expect 201)"
[[ "$(status_of POST /memos "" -H "X-Device-Id: ${DEVICE_ID}" -H "X-Memo-Id: ${MEMO_ID}" \
    -H "Content-Type: application/octet-stream" --data-binary "@${FIXTURE}")" == "201" ]] \
    || fail "upload: $(cat "${WORK_DIR}/body")"

echo "== upload again (expect 200, idempotent)"
[[ "$(status_of POST /memos "" -H "X-Device-Id: ${DEVICE_ID}" -H "X-Memo-Id: ${MEMO_ID}" \
    -H "Content-Type: application/octet-stream" --data-binary "@${FIXTURE}")" == "200" ]] \
    || fail "replay: $(cat "${WORK_DIR}/body")"

echo "== wait for transcription"
ready=""
for _ in $(seq 1 60); do
    status_of GET "/memos/${MEMO_ID}" "" >/dev/null
    if [[ "$(json_field status)" == "ready" ]]; then ready=1; break; fi
    sleep 0.25
done
[[ -n "${ready}" ]] || fail "memo never became ready: $(cat "${WORK_DIR}/body")"
[[ -n "$(json_field transcript)" ]] || fail "ready memo has no transcript"

echo "== list"
status_of GET "/memos?limit=10" "" >/dev/null
[[ "$(json_field items.0.id)" == "${MEMO_ID}" ]] || fail "list did not return the memo"
[[ -n "$(json_field items.0.snippet)" ]] || fail "list snippet is empty"

echo "== patch transcript (expect 200)"
[[ "$(status_of PATCH "/memos/${MEMO_ID}" '{"transcript":"手动修正的文本","lang":"zh"}' \
    -H "Content-Type: application/json")" == "200" ]] || fail "patch: $(cat "${WORK_DIR}/body")"
[[ "$(json_field transcript)" == "手动修正的文本" ]] || fail "patch did not stick"

echo "== patch rejects unknown field (expect 400)"
[[ "$(status_of PATCH "/memos/${MEMO_ID}" '{"titel":"typo"}' -H "Content-Type: application/json")" == "400" ]] \
    || fail "unknown field was accepted"

echo "== audio (expect 200 + RIFF), range (expect 206)"
[[ "$(status_of GET "/memos/${MEMO_ID}/audio" "")" == "200" ]] || fail "audio download failed"
head -c 4 "${WORK_DIR}/body" | grep -q RIFF || fail "audio is not a wav"
[[ "$(curl -sS -o /dev/null -w '%{http_code}' -H 'Range: bytes=0-99' "${BASE}/memos/${MEMO_ID}/audio")" == "206" ]] \
    || fail "range request did not answer 206"

echo "== re-transcribe (expect 202)"
[[ "$(status_of POST "/memos/${MEMO_ID}/transcribe" "")" == "202" ]] || fail "transcribe: $(cat "${WORK_DIR}/body")"

echo "== delete (expect 204), then 404"
[[ "$(status_of DELETE "/memos/${MEMO_ID}" "")" == "204" ]] || fail "delete failed"
[[ "$(status_of GET "/memos/${MEMO_ID}" "")" == "404" ]] || fail "deleted memo is still readable"

echo
echo "PASS: upload, idempotency, transcription, list, patch, audio range, retranscribe, delete"
