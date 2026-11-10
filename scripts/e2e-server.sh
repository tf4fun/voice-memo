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

for _ in $(seq 1 150); do
    if curl -sS -o /dev/null "${BASE}/ping" 2>/dev/null; then break; fi
    if ! kill -0 "${SERVER_PID}" 2>/dev/null; then break; fi
    sleep 0.1
done
if ! curl -sS "${BASE}/ping" >/dev/null; then
    sed -n '1,40p' "${WORK_DIR}/server.log" >&2
    fail "server did not come up"
fi

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

echo "== history pages and bounded versioned text"
echo "== embedded web manager and browser WAV playback"
python3 - "${BASE}" "${MEMO_ID}" <<'PY_WEB'
import sys, urllib.request, urllib.error
base, memo_id = sys.argv[1:]
origin = base.removesuffix('/api/v1')
def get(url, headers=None):
    with urllib.request.urlopen(urllib.request.Request(url, headers=headers or {}), timeout=10) as response:
        return response.status, response.headers, response.read()
status, headers, page = get(origin + '/')
assert status == 200 and b'text/html' in headers['Content-Type'].encode()
assert ('/memos/' + memo_id).encode() in page
_, _, detail = get(origin + '/memos/' + memo_id)
assert '手动修正的文本'.encode() in detail and b'audio.wav' in detail
status, headers, wav = get(base + '/memos/' + memo_id + '/audio.wav', {'Range':'bytes=0-43'})
assert status == 206 and headers['Content-Type'] == 'audio/wav'
assert len(wav) == 44 and wav[:4] == b'RIFF' and wav[8:12] == b'WAVE'
for asset in ('app.css', 'app.js'):
    status, _, data = get(origin + '/assets/' + asset)
    assert status == 200 and data
request = urllib.request.Request(base + '/memos/' + memo_id, method='DELETE', headers={'Origin':'https://other.example'})
try:
    urllib.request.urlopen(request, timeout=10)
    raise AssertionError('cross-origin deletion accepted')
except urllib.error.HTTPError as error:
    assert error.code == 403
PY_WEB

echo "== history pages and bounded versioned text"
python3 - "${BASE}" "${FIXTURE}" "${MEMO_ID}" "${DEVICE_ID}" <<'PY_PAGES'
import json, sys, urllib.request, urllib.parse, urllib.error
base, fixture, original_id, device = sys.argv[1:]
def request(method, path, body=None, headers=None):
    req=urllib.request.Request(base+path, data=body, method=method, headers=headers or {})
    with urllib.request.urlopen(req, timeout=10) as response:
        data=response.read()
        return response.status, json.loads(data) if data else None
raw=bytearray(open(fixture,"rb").read())
for i in range(1,51):
    memo_id=f"{i:08x}-0000-4000-8000-000000000000"
    raw[24:60]=memo_id.encode()
    status,_=request("POST","/memos",bytes(raw),{"X-Device-Id":device,"X-Memo-Id":memo_id,"Content-Type":"application/octet-stream"})
    assert status==201
pages=[];cursor=""
while True:
    _,page=request("GET","/memos?limit=24&cursor="+urllib.parse.quote(cursor))
    pages.append(page)
    if not page["next_cursor"]: break
    cursor=page["next_cursor"]
    assert len(pages)<10
ids=[m["id"] for page in pages for m in page["items"]]
assert len(ids)==51 and len(set(ids))==51
current=pages[-1]
for expected in reversed(pages[:-1]):
    _,current=request("GET","/memos?limit=24&direction=newer&cursor="+urllib.parse.quote(current["previous_cursor"]))
    assert [m["id"] for m in current["items"]]==[m["id"] for m in expected["items"]]
assert current["previous_cursor"]==""
want=("\u4e2da\U0001f600\n"*4000)
request("PATCH","/memos/"+original_id,json.dumps({"transcript":want}).encode(),{"Content-Type":"application/json"})
text=[];offset=0;revision="";last=None
while True:
    _,last=request("GET","/memos/"+original_id+"/text?"+urllib.parse.urlencode({"offset":offset,"revision":revision,"limit":2048}))
    assert last["offset"]==offset and len(last["text"].encode())<=2048
    text.append(last["text"]);offset=last["end"];revision=last["revision"]
    if offset==last["total"]: break
assert "".join(text)==want
reverse=[last["text"]]
while last["offset"]:
    _,last=request("GET","/memos/"+original_id+"/text?"+urllib.parse.urlencode({"offset":last["offset"],"revision":revision,"direction":"previous","limit":2048}))
    reverse.append(last["text"])
assert "".join(reversed(reverse))==want
request("PATCH","/memos/"+original_id,b'{"transcript":"edited again"}',{"Content-Type":"application/json"})
try:
    request("GET","/memos/"+original_id+"/text?revision="+revision)
    raise AssertionError("stale revision accepted")
except urllib.error.HTTPError as error:
    assert error.code==409
PY_PAGES

echo "== gc preview/apply (keep accepted audio and rejected files)"
python3 - "${WORK_DIR}/data" <<'PY_GC'
from pathlib import Path
import os, sys, time
root=Path(sys.argv[1])
for name in ("incoming/gc-stale.part", "audio/gc-orphan.memo", "rejected/gc-diagnostic.memo"):
    path=root/name
    path.write_bytes(b"gc-test")
    os.utime(path,(time.time()-172800,time.time()-172800))
for path in root.glob("audio/*/*/*.memo"):
    os.utime(path,(time.time()-172800,time.time()-172800))
PY_GC
MEMO_DATA_DIR="${WORK_DIR}/data" "${WORK_DIR}/memo-server" gc >"${WORK_DIR}/gc-preview"
[[ -f "${WORK_DIR}/data/incoming/gc-stale.part" ]] || fail "dry-run removed a partial"
[[ -f "${WORK_DIR}/data/audio/gc-orphan.memo" ]] || fail "dry-run removed an orphan"
MEMO_DATA_DIR="${WORK_DIR}/data" "${WORK_DIR}/memo-server" gc --apply >"${WORK_DIR}/gc-apply"
[[ ! -e "${WORK_DIR}/data/incoming/gc-stale.part" ]] || fail "stale partial survived gc"
[[ ! -e "${WORK_DIR}/data/audio/gc-orphan.memo" ]] || fail "orphan survived gc"
[[ -f "${WORK_DIR}/data/rejected/gc-diagnostic.memo" ]] || fail "gc removed a rejected recording"
if MEMO_DATA_DIR="${WORK_DIR}/data" "${WORK_DIR}/memo-server" gc --apply --older-than 1h >/dev/null 2>&1; then
    fail "gc accepted an unsafe age"
fi

echo "== audio (expect original MEMO bytes), range (expect 206)"
[[ "$(status_of GET "/memos/${MEMO_ID}/audio" "")" == "200" ]] || fail "audio download failed"
cmp -s "${FIXTURE}" "${WORK_DIR}/body" || fail "audio differs from original container"
[[ "$(curl -sS -D "${WORK_DIR}/headers" -o "${WORK_DIR}/range" -w '%{http_code}' \
    -H 'Range: bytes=0-99' "${BASE}/memos/${MEMO_ID}/audio")" == "206" ]] \
    || fail "range request did not answer 206"
python3 - "${FIXTURE}" "${WORK_DIR}/range" "${WORK_DIR}/headers" <<'PY_CHECK'
from pathlib import Path
import sys
fixture, partial, headers = map(Path, sys.argv[1:])
assert partial.read_bytes() == fixture.read_bytes()[:100], "range bytes differ"
headers = headers.read_text().lower()
assert "content-type: application/octet-stream" in headers
assert "content-range: bytes 0-99/" in headers
PY_CHECK

echo "== re-transcribe (expect 202)"
[[ "$(status_of POST "/memos/${MEMO_ID}/transcribe" "")" == "202" ]] || fail "transcribe: $(cat "${WORK_DIR}/body")"

echo "== delete (expect 204), then 404"
[[ "$(status_of DELETE "/memos/${MEMO_ID}" "")" == "204" ]] || fail "delete failed"
[[ "$(status_of GET "/memos/${MEMO_ID}" "")" == "404" ]] || fail "deleted memo is still readable"
[[ "$(status_of POST /memos "" -H "X-Device-Id: ${DEVICE_ID}" -H "X-Memo-Id: ${MEMO_ID}" \
    -H "Content-Type: application/octet-stream" --data-binary "@${FIXTURE}")" == "410" ]] \
    || fail "deleted memo replay was not terminal"


echo
echo "PASS: upload, idempotency, transcription, bidirectional history, versioned long text, patch, safe gc, audio range, retranscribe, delete"
