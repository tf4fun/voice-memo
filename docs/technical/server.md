<p align="right">
  <a href="server.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Voice Memo Server — Technical Design

Implementation snapshot: 2026-10-27. Describes the current Go server, its storage
and HTTP contract. The [client design](client.md) covers firmware; work and
acceptance are tracked only in the [milestones](../project/milestones.md).

## 1. Scope and runtime

One Go process receives recordings, stores the original Opus containers, runs
background transcription and serves metadata/audio to LAN clients. It uses
Go 1.27.3, `net/http`, SQLite through `modernc.org/sqlite`, `pion/opus` for decode
and `grandcat/zeroconf` for mDNS. There is no external database, broker, ffmpeg
or cgo requirement. There is no web UI, authentication or TLS listener.

## 2. Package layout

| Path under `server/` | Responsibility |
| --- | --- |
| `cmd/memo-server` | `serve`, `gc`, `version`, configuration and process lifecycle |
| `cmd/memogen` | Generate a MEMO fixture from an Ogg Opus file |
| `internal/config` | Environment parsing and validation |
| `internal/domain` | Memo/status types, container header and frame reader |
| `internal/audio` | Structural validation, Opus decode and WAV output |
| `internal/store` | SQLite schema, queries, job persistence |
| `internal/httpapi` | HTTP routes, JSON, uploads, audio and SSE |
| `internal/asr` | Transcriber interface, SiliconFlow and fake implementations |
| `internal/jobs` | One transcription runner |
| `internal/events` | In-memory event broadcast hub |
| `internal/discovery` | LAN service advertisement |

## 3. Storage and lifecycle

SQLite uses WAL and schema version 1. `devices` records source IDs and first/last
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
result and another device receives 409. Unique partial names avoid collisions.
The partial file and destination directory are synchronised before acceptance.
The body is byte-capped while streaming to a partial file; deferred cleanup
removes ordinary failed partial uploads. Structural validation failures reaching
`keepRejected` retain the container under `rejected/`; not every rejection does.
There is no startup sweep of abandoned partial or orphaned files.

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
errors, including provider 429, are retryable. One runner polls every 2 s and
processes due jobs sequentially. Maximum attempts are three: the first transient
failure schedules +30 s, the second +2 min, and the third fails permanently.
Although the backoff array includes 10 min, the three-attempt policy never
schedules that delay. `MEMO_ASR_CONCURRENCY` is parsed but does not create extra
runners.

Status flow is `uploaded → transcribing → ready`, or retry back to `uploaded`,
or terminal `failed`. Startup calls `RecoverJobs` before starting the worker to
recover interrupted transcription. Initial memo insertion and job enqueue share one
transaction; enqueue failure rolls back the memo and the handler removes its
newly published audio. Replays do not enqueue another job.

## 5. Discovery

mDNS advertises `_voicememo._tcp` with configurable instance name (hostname by
default) and the HTTP port. TXT includes `ver=1`, `path=/api/v1`, `caps=opus`,
and `ip=` when a non-loopback IPv4 is available. The first such interface
address is used; it is not explicitly selected by route to the device.
The client can fall back to TXT when SRV/A resolution is incomplete.

Advertisement failure is logged without stopping HTTP. Multicast requires
network visibility; a native process or host-network container provides direct
LAN access. Firmware has no manual server-address entry.

## 6. HTTP routes

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
| DELETE | `/memos/{id}` | Soft-delete row/drop job and attempt audio removal; 204, unknown ID 404 |
| GET | `/memos/{id}/audio` | Original MEMO bytes, `application/octet-stream`, Range supported |
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
100 and returns `items` plus `next_cursor`. The cursor encodes `recorded_at:id`.
There is no time-range or transcript-search parameter. Items include `id`,
`device_id`, `recorded_at`, `uploaded_at`, `duration_ms`, `status`, optional
`lang` and the first 40 transcript runes (`snippet`, with ellipsis if truncated).

Detail adds nonzero/nonempty `size_bytes`, `codec`, `transcript`, `edited_at`,
`error`, ASR metadata and audio path/size. Optional empty values are omitted,
not emitted as JSON null. `audio.bytes` is the original container size.

`PATCH` rejects unknown fields and accepts `transcript` and/or `lang` only.
Deleting is not reversible through the API: the row is hidden and audio removal
is attempted immediately. A filesystem removal failure is logged. The audio
endpoint uses `http.ServeContent`; byte ranges address container bytes, not PCM.
Firmware currently starts each playback from the beginning.

## 7. Errors and events

Errors use `{"error":{"code":"...","message":"..."}}`.

| Code | HTTP status | Meaning |
| --- | --- | --- |
| `not_found` | 404 | Unknown/deleted memo or missing audio |
| `invalid_json` | 400 | Invalid body, unknown field, limit or cursor |
| `missing_device` | 400 | Missing upload source ID |
| `bad_container` | 400 | Container/header/ID or structural validation error |
| `frame_mismatch` | 400 | Errors wrapped as `ErrFrameMismatch` |
| `payload_too_large` | 413 | Body exceeds byte cap |
| `id_conflict` | 409 | Existing UUID belongs to another device |
| `over_limit` | 400 | Header duration exceeds configured cap |
| `internal` | 500 | Storage/query or other server failure |

SSE starts with `event: ready` and `data: {}`, followed by `event: memo` JSON
containing `type`, `id`, `status`, optional `snippet`, `recorded_at` and
`duration_ms`. A comment heartbeat is emitted every 20 s. Upload and worker
status changes publish events; PATCH, DELETE and manual requeue do not publish
separate management notifications.

The hub keeps eight events per subscriber and skips delivery when its buffer is
full. It has no persistent event log, replay cursor or guaranteed delivery;
clients reconcile through list reads. Worker snippets carry the full transcript,
while list snippets are limited to 40 runes.

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
| `memo-server gc` | Validate config/open store, print a placeholder; no sweeping |

| Variable | Default | Current use |
| --- | --- | --- |
| `MEMO_ADDR` | `:8080` | Listen address |
| `MEMO_DATA_DIR` | `./data` | Database/audio root |
| `MEMO_MDNS_NAME` | hostname | Advertised instance |
| `MEMO_ASR_PROVIDER` | `siliconflow` | `siliconflow` or `fake` only |
| `MEMO_ASR_API_KEY` | empty | Required at startup for SiliconFlow |
| `MEMO_ASR_MODEL` | `FunAudioLLM/SenseVoiceSmall` | Multipart model |
| `MEMO_ASR_BASE_URL` | `https://api.siliconflow.cn` | Provider URL prefix |
| `MEMO_ASR_CONCURRENCY` | `1` | Validated positive; runner count remains one |
| `MEMO_MAX_BYTES` | `33554432` | Streaming upload cap |
| `MEMO_MAX_MEMO_MS` | `300000` | Declared and frame-derived duration cap, advertised to client |
| `MEMO_RETENTION_DAYS` | `0` | Validated nonnegative; no retention sweep |
| `MEMO_LOG_LEVEL` | `info` | debug/info/warn/error |

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

`scripts/e2e-server.sh` runs a real process with fake ASR. Its current audio
assertion still expects RIFF/WAV although the endpoint now returns MEMO; that
script does not establish end-to-end success for the current contract.
Actual results and follow-up checks live in the [milestones](../project/milestones.md).
