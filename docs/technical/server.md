<p align="right">
  <a href="server.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Voice Memo Server — Technical Design

Implementation snapshot: 2026-11-10. Describes the current Go server, its storage
and HTTP contract. The [client design](client.md) covers firmware; work and
acceptance are tracked only in the [milestones](../project/milestones.md).

## 1. Scope and runtime

One Go process receives recordings, stores the original Opus containers, runs
background transcription and serves metadata/audio to LAN clients. It uses
Go 1.27.3, `net/http`, SQLite through `modernc.org/sqlite`, `pion/opus` for decode
and `grandcat/zeroconf` for mDNS. There is no external database, broker, ffmpeg
or cgo requirement. A small embedded web manager uses only Go's standard library
for HTTP, HTML templates and asset embedding, plus plain CSS/JavaScript. No
frontend packages or asset build are required. There is no authentication or TLS listener.

## 2. Package layout

| Path under `server/` | Responsibility |
| --- | --- |
| `cmd/memo-server` | `serve`, `gc`, `version`, configuration and process lifecycle |
| `cmd/memogen` | Generate a MEMO fixture from an Ogg Opus file |
| `internal/config` | Environment parsing and validation |
| `internal/domain` | Memo/status types, container header and frame reader |
| `internal/audio` | Structural validation, Opus decode and WAV output |
| `internal/store` | SQLite schema, queries, job persistence |
| `internal/httpapi` | HTTP routes, embedded web manager, JSON, uploads, audio and SSE |
| `internal/asr` | Transcriber interface, SiliconFlow and fake implementations |
| `internal/jobs` | Bounded transcription pool and acknowledged shutdown |
| `internal/events` | In-memory event broadcast hub |
| `internal/discovery` | LAN service advertisement |

## 3. Storage and lifecycle

SQLite uses WAL, `synchronous=FULL` and schema version 1. `devices` records source IDs and first/last
seen times. `memos` holds recording/upload times, duration/frame/byte counts,
codec, status, transcript/language, ASR identity/trace, edit/error fields,
audio path, clock skew and soft-deletion time. `jobs` holds a unique memo ID,
attempt count and next due time. The list index orders live rows by
`recorded_at DESC, id DESC`.

Files live under `MEMO_DATA_DIR`:

```text
memo.db
incoming/<UUID>-<random>.part
audio/<device_id>/<YYYYMM>/<UUID>.memo
rejected/<UUID>.memo
```

The original container is canonical. Upload structurally validates it without
decoding Opus, then publishes it without replacing existing audio and commits
metadata plus its initial job in one SQLite transaction. Same-ID requests are
serialised with bounded lock stripes; same-device replay returns the existing
result and another device receives 409. A deleted UUID returns 410 without
recreating audio/jobs, so devices treat it as a terminal rejection. Unique partial names avoid collisions.
The partial file and destination directory chain through the data root are
synchronised before acceptance.
The body is byte-capped while streaming to a partial file; deferred cleanup
removes ordinary failed partial uploads. Structural validation failures reaching
`keepRejected` retain the container under `rejected/`; not every rejection does.
There is no automatic sweep. Explicit `gc` previews stale partial/orphan files;
`gc --apply` removes only eligible files, never recording rows or rejected files.

The SiliconFlow adapter decodes the container into an in-memory WAV only for
the transcription request. WAV is not stored as the canonical audio file.
Accepted containers with undecodable Opus payloads can therefore fail later at
transcription time. The fake provider returns fixed text without decoding.

`recorded_at` comes from the container. Zero or a difference over 600 seconds
from receipt time substitutes server time and records the delta as `clock_skew`.
The server uses that corrected time for list ordering and the monthly path.

## 4. Transcription

`asr.Transcriber` exposes `Name()` and
`Transcribe(context.Context, asr.Input) (asr.Result, error)`. Input carries the
stored container path, duration and language. Result carries text, language,
provider, model and trace ID. `ErrPermanent` distinguishes non-retryable errors.

SiliconFlow posts multipart `file` (decoded WAV) and `model` to
`<MEMO_ASR_BASE_URL>/v1/audio/transcriptions` using a bearer key. It reads `text`
and `x-siliconcloud-trace-id`. The adapter enforces its own 50 MiB/one-hour
bounds and uses a default 120 s HTTP timeout. These are code constants, not a
claim about the provider's latest service policy.

Provider 400/401/403/413/415 and local decode errors are permanent; other HTTP
errors, including provider 429, are retryable. The configured 1–4 workers claim
jobs independently and poll every 2 s. Each attempt has a 90 s context deadline,
shorter than the 120 s claim lease; container reads and HTTP honour cancellation.
Maximum attempts remain three, with +30 s then +2 min transient retries.

A claim contains the job row ID and attempt number. Status/result/queue changes
commit together only if that exact claim still owns the memo. Manual editing
sets ready and removes the job; deletion removes it; requeue atomically creates
a fresh autoincrement job ID. Late results or old lease attempts cannot overwrite
those mutations or remove a replacement job. Worker shutdown cancels and joins
the pool before closing SQLite. Interrupted jobs remain available for recovery.

Status flow is `uploaded → transcribing → ready`, retry to `uploaded`, or terminal
`failed`. Startup recovers interrupted jobs. Initial memo/job acceptance is one
transaction; replay never creates another job.

## 5. Discovery

mDNS advertises `_voicememo._tcp` on one active multicast IPv4 LAN. The
configurable instance name defaults to the hostname, with a trailing `.local`
removed and dot/punctuation labels normalized into a single UTF-8 label of at
most 63 bytes. An explicit ASCII `.local.` host is registered with that LAN's
IPv4, avoiding the bundled registrar's macOS `.local.local` target. TXT includes
`ver=1`, `path=/api/v1`, `caps=opus` and the same `ip=` as the A record.
The client can fall back to TXT when SRV/A resolution is incomplete.

Selection excludes down, loopback, non-multicast and point-to-point interfaces,
and unusable/link-local IPv4 addresses. The first eligible interface is used;
`MEMO_MDNS_INTERFACE` can select one explicitly. An unusable explicit selection
reports advertisement failure without falling back to another network. Changes
in interfaces/addresses require a restart. Loopback-only HTTP listeners do not
advertise: local tests must not replace a live LAN service's records.

Advertisement failure is logged without stopping HTTP. Multicast requires
network visibility; a native process or host-network container provides direct
LAN access. Firmware has no manual server-address entry.

## 6. HTTP routes

The same listener serves the Chinese web manager at `/`: twenty recordings per
cursor page, device filtering, older/newer navigation and an empty/error state.
`/memos/{id}` shows a recording's transcript, metadata and audio player. Plain
JavaScript reuses the existing PATCH/DELETE/retranscription APIs; destructive
actions require an in-page confirmation, failed edits retain their draft, and
leaving with unsaved text prompts the browser. Pending transcription status polls
every four seconds while the page is visible and not being edited. Late poll
responses cannot overwrite a newer edit. List refresh is manual.

Templates escape recording text; CSS/JavaScript are embedded at `/assets/app.css`
and `/assets/app.js`. HTML responses are not cached and carry a same-origin
content policy. Standard-library cross-origin protection rejects unsafe browser
requests from other origins while allowing firmware requests without browser
origin headers. This is request protection, not user authentication.

Browser playback/download uses `/api/v1/memos/{id}/audio.wav`. The existing Opus
decoder writes to a private temporary WAV file, deleted when the request ends;
no PCM cache is added to RAM or persistent storage. Conversion and serving share
one slot per process; another preview receives 503 with `Retry-After: 2`. Decode
checks cancellation and caps PCM output at the frame-derived size, retaining the
container's declared-duration tolerance. WAV responses support HEAD/Range and
are not cached. The original `/audio` route remains MEMO bytes for the device.

All paths below are prefixed with `/api/v1`. JSON is used except for upload,
audio and SSE. Reads expose all devices unless the optional list filter is used.

| Method | Path | Current response/behaviour |
| --- | --- | --- |
| GET | `/health` | Service/version, schema, DB status, time, uptime; 503 if DB ping fails |
| GET | `/ping` | 200, `{"service":"memo","api":1}` |
| GET | `/server` | Version, API, clock, provider/model, limits and queue depth |
| POST | `/memos` | Upload; 201 new, 200 existing UUID |
| GET | `/memos` | Cursor list, newest first |
| GET | `/memos/{id}` | Detail, transcript, ASR and audio metadata |
| PATCH | `/memos/{id}` | Change transcript and/or `lang`, stamp edit time, set `ready` |
| DELETE | `/memos/{id}` | Atomically hide row/drop job, remove audio; 204 including known-ID retries, unknown ID 404, removal failure 500 |
| GET | `/memos/{id}/audio` | Original MEMO bytes, `application/octet-stream`, Range supported |
| GET | `/memos/{id}/audio.wav` | On-demand browser WAV preview/download, HEAD/Range supported |
| GET | `/memos/{id}/text` | Bounded UTF-8 display window with byte offsets and content revision |
| POST | `/memos/{id}/transcribe` | Set `uploaded`, clear error and enqueue; 202 |
| GET | `/events` | `text/event-stream` subscription |

### Upload

```http
POST /api/v1/memos
Content-Type: application/octet-stream
X-Device-Id: A1B2C3D4E5F6
X-Memo-Id: 00000000-0000-4000-8000-000000000000

<MEMO container bytes>
```

`X-Device-Id` is trimmed and must match `[A-Za-z0-9_-]{1,64}`. `X-Memo-Id` must have dashed UUID syntax and
match the container. Recording time comes from the container, not a separate
`X-Recorded-At` header. A known non-deleted UUID returns its existing result
without making another job. Default limits are 32 MiB and 300000 ms.

```json
{"id":"00000000-0000-4000-8000-000000000000","status":"uploaded","duration_ms":1000}
```

### List and detail

`GET /memos?limit=20&cursor=<opaque>&device=<optional>` defaults to 20, caps at
100 and returns `items`, `next_cursor` and `previous_cursor`. The cursor encodes
`recorded_at:id`. Default `direction=older` continues below its anchor;
`direction=newer` requires a cursor and selects the nearest newer page. Both
return newest-first order and retain the device filter. Page rows and cursor
existence checks share one SQLite transaction. Exact final pages do not offer a
spurious next cursor; a page removed by deletion retains a navigation escape.
There is no time-range or transcript-search parameter. Items include `id`,
`device_id`, `recorded_at`, `uploaded_at`, `duration_ms`, `status`, optional
`lang` and the first 40 transcript runes (`snippet`, with ellipsis if truncated).

Detail adds nonzero/nonempty `size_bytes`, `codec`, `transcript`, `edited_at`,
`error`, ASR metadata and audio path/size. Optional empty values are omitted,
not emitted as JSON null. `audio.bytes` is the original container size.

`GET /memos/{id}/text?offset=0&limit=2048&direction=next&revision=<optional>`
reads a display window without sending the full transcript. Limit is 4–4096
bytes, default 2048; direction is `next` or `previous`. Offsets count UTF-8 bytes
and must be character boundaries. A forward window begins at offset; a previous
window ends there. Responses include `id`, `status`, `text`, `offset`, `end`,
`total` and a 64-character SHA-256 `revision`. Next uses `end`, previous uses
`offset`, so clients keep one window regardless of total text length.

The display representation replaces NUL and invalid UTF-8 with U+FFFD. Revision
hashes this representation; a supplied stale revision returns 409
`transcript_changed`, requiring a fresh read. Empty text still returns status and
zero bounds. Missing/deleted memos return 404. The full-detail endpoint remains
unchanged; forward-only list clients can ignore the additive cursor field.

`PATCH` rejects unknown fields and accepts `transcript` and/or `lang` only.
Deleting is not reversible through the API: the row is hidden and audio removal
is attempted immediately. Hiding the row and revoking its job share one SQLite
transaction; failure rolls both back. A filesystem removal failure is logged,
publishes the metadata deletion and returns 500. Repeating DELETE for a known
deleted ID retries removal without changing its original deletion timestamp;
successful/missing-file cleanup returns 204. Unknown IDs remain 404. The audio
endpoint uses `http.ServeContent`; byte ranges address container bytes, not PCM.
Firmware currently starts each playback from the beginning.

## 7. Errors and events

Errors use `{"error":{"code":"...","message":"..."}}`.

| Code | HTTP status | Meaning |
| --- | --- | --- |
| `not_found` | 404 | Unknown/deleted memo or missing audio |
| `invalid_json` | 400 | Invalid body, unknown field, limit, cursor, direction or text boundary |
| `missing_device` | 400 | Missing upload source ID |
| `bad_container` | 400 | Container/header/ID or structural validation error |
| `frame_mismatch` | 400 | Errors wrapped as `ErrFrameMismatch` |
| `payload_too_large` | 413 | Body exceeds byte cap |
| `id_conflict` | 409 | Existing UUID belongs to another device |
| `transcript_changed` | 409 | Display text revision changed; restart window reading |
| `deleted` | 410 | UUID explicitly deleted; replay does not recreate it |
| `over_limit` | 400 | Declared or frame-derived duration exceeds cap |
| `internal` | 500 | Storage/query or other server failure |

SSE subscribes before sending `event: ready` / `data: {}`. Upload, worker,
PATCH, DELETE and manual requeue all publish mutation events. DELETE carries
`type=deleted`; clients reconcile authoritative list state rather than merging
partial event rows. Every snippet is limited centrally to 40 runes plus an
ellipsis; escaped JSON still fits the firmware's 512-byte framing bound.

A comment heartbeat is emitted every 20 s; writes have a 5 s deadline. The hub
buffers eight events per subscriber and closes slow subscribers on overflow,
forcing reconnect/list reconciliation. There is no persistent replay log or
exactly-once guarantee; reconnect refresh and periodic polling reconcile gaps.

## 8. Upload container

Version 1, little-endian integer fields:

| Offset | Size | Field |
| --- | ---: | --- |
| 0 | 4 | Magic `MEMO` |
| 4 | 1 | Version 1 |
| 5 | 1 | Codec 1, Opus |
| 6 | 2 | Sample rate; firmware writes 16000 |
| 8 | 1 | Channels; firmware writes 1 |
| 9 | 1 | Frame duration in ms; firmware writes 60 |
| 10 | 2 | Reserved |
| 12 | 4 | Duration in ms |
| 16 | 8 | UTC recording seconds, zero if unsynchronised |
| 24 | 36 | Dashed ASCII UUID |
| 60 | 4 | Frame count |
| 64 | 16 | Reserved |
| 80 | variable | Repeated uint16 length followed by Opus packet |

Header parsing accepts sample rates 8000–48000, channels 1 or 2, and frame
lengths 1–60 ms; actual Opus decoding also imposes decoder support. Upload walks
all packet boundaries, rejecting empty recordings, empty/truncated packets and
any declared/actual frame-count mismatch, including zero declarations. Count
and duration mismatches map to `frame_mismatch`.

Declared duration must agree with `frame_count × frame_ms` within 5%. Both the
declared and frame-derived duration must fit the configured cap. Upload does
not decode payloads; later transcription can still reject undecodable Opus.

## 9. CLI and configuration

| Command | Behaviour |
| --- | --- |
| `memo-server serve` | HTTP, job worker and advertisement; default command |
| `memo-server serve -addr host:port` | Override listen address |
| `memo-server version` | Print `0.1.0` |
| `memo-server gc` | Dry-run stale unreferenced `.part`/`.memo` candidates, minimum age 24 h |
| `memo-server gc --apply` | Delete eligible candidates; keep referenced audio, rejected files and all rows |

| Variable | Default | Current use |
| --- | --- | --- |
| `MEMO_ADDR` | `:8080` | Listen address |
| `MEMO_DATA_DIR` | `./data` | Database/audio root |
| `MEMO_MDNS_NAME` | hostname | Normalized single-label instance |
| `MEMO_MDNS_INTERFACE` | auto | One active multicast IPv4 LAN; e.g. `en0` on this Mac |
| `MEMO_ASR_PROVIDER` | `siliconflow` | `siliconflow` or `fake` only |
| `MEMO_ASR_API_KEY` | empty | Required at startup for SiliconFlow |
| `MEMO_ASR_MODEL` | `FunAudioLLM/SenseVoiceSmall` | Multipart model |
| `MEMO_ASR_BASE_URL` | `https://api.siliconflow.cn` | Provider URL prefix |
| `MEMO_ASR_CONCURRENCY` | `1` | Actual worker count, range 1–4 |
| `MEMO_MAX_BYTES` | `33554432` | Streaming upload cap |
| `MEMO_MAX_MEMO_MS` | `300000` | Declared and frame-derived duration cap, advertised to client |
| `MEMO_RETENTION_DAYS` | `0` | Only zero supported; nonzero fails startup, no automatic expiry |
| `MEMO_LOG_LEVEL` | `info` | debug/info/warn/error |

`gc` needs only `MEMO_DATA_DIR`, not ASR credentials. `--older-than` defaults to
24 h and refuses shorter ages. It skips symlinks, recent files, unknown file
types, all referenced paths (including tombstones) and `rejected/`. Apply rechecks
identity/mtime/size before deletion. HTTP upload reads have a 120 s deadline,
shorter than the GC age floor. There is no automatic voice-retention deletion.

## 10. Security and deployment

The service is intended for a trusted single-user LAN. Anyone reaching its port
can read, edit and delete recordings; source IDs are attribution, not identity
verification. Upload validates UUID syntax/matching and constrains trimmed device
IDs to 1–64 ASCII letters, digits, underscores or hyphens before forming a path.

The ASR key is environment configuration and sent as a bearer header. Provider
error response snippets can be stored or logged; arbitrary provider text is not
sanitised by this adapter. No built-in TLS, token or account middleware exists.

Build from the repository root:

```bash
source scripts/go-env.sh
cd server
mkdir -p ../build
CGO_ENABLED=0 go build -trimpath -ldflags="-s -w" -o ../build/memo-server ./cmd/memo-server
MEMO_ASR_PROVIDER=fake ../build/memo-server serve
```

Persist and back up the data directory, including SQLite and the container tree.
For a live SQLite database use a consistent database backup rather than copying
only `memo.db` while WAL writes continue. Migrations run when opening the store.

## 11. Validation interfaces

`cd server && go test ./...` covers configuration, header/frame parsing,
decoding fixtures, SQLite operations, API flows, provider request/error handling
and job retries. Tests use temporary storage, fake transcription and local HTTP
servers; a first run may need module/toolchain downloads.

Web handler tests cover HTML escaping, device-filtered pagination, mutation
origin checks, WAV playback/ranges, conversion bounds and temporary-file cleanup.
`node --test tests/test_memo_web.js` from the repository root uses only Node's
built-ins to check edit failure recovery, stale-poll suppression and confirmation
flows; Node is a test tool, not a server build/runtime dependency. Actual browser
layout and media controls still require browser acceptance.

`scripts/e2e-server.sh` runs a real process with fake ASR and temporary storage.
It compares original MEMO audio bytes and exact Range responses, and covers
upload/idempotency, transcription, bidirectional history, versioned text,
management, retranscription and safe GC. It does not call the live provider.
Actual results and follow-up checks live in the [milestones](../project/milestones.md).


### Local SiliconFlow startup

The existing adapter calls the [SiliconFlow transcription endpoint](https://docs.siliconflow.cn/docs/api/audio-transcriptions-post)
with a decoded WAV and the configured model. `MEMO_ASR_BASE_URL` is the origin
(e.g. `https://api.siliconflow.cn`); the adapter appends `/v1/audio/transcriptions`.
Model selection stays in `MEMO_ASR_MODEL`; the launcher does not override it.

For local operation, create `server/.env.local` from `server/.env.example` only
when no private configuration already exists, set its API key, and restrict file
permissions to the owner (`chmod 600 server/.env.local`). Start from the repository
root with `./scripts/run-server.sh`. It reuses the repository Go cache setup,
builds `build/server/memo-server`, and runs in `server/`; the default data root
is therefore `server/data/`. `MEMO_ENV_FILE` selects another configuration file.
The private file, default data directory and generated binaries/logs are ignored
by Git. API keys remain server-side and are never put into device firmware.

The file format is literal `MEMO_NAME=value`, with blank lines, comment-only
lines, CRLF and optional matching outer quotes. It does not execute shell
commands, interpolate variables or support inline comments. File entries override
inherited environment values of the same name. Unsupported names or malformed
lines fail without printing values. `./scripts/run-server.sh --check-env` checks
file syntax only; it does not validate credentials or make an API call.

The foreground process must remain running. File changes require a restart;
no login/boot service is installed. A device on the same LAN discovers the
running service through mDNS. Local health/configuration endpoints are
`http://127.0.0.1:8080/api/v1/health` and `/api/v1/server` with the default port.
Provider-mock tests check the exact HTTP path, multipart fields and RIFF/WAVE
payload. A real provider test is separate from those mocks and from on-device
speech acceptance. Local launch/parser tests need only Python and Bash.
