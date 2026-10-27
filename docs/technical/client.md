<p align="right">
  <a href="client.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Voice Memo Client — Technical Design

Implementation snapshot: 2026-10-27. This document describes the compiled
firmware. Current user behaviour is in the [product specification](../product/product-spec.md);
work and validation are tracked only in the [milestones](../project/milestones.md).
The server wire contract is in the [server design](server.md).

## 1. Platform and build boundary

ESP32-C3, 8 MB Flash, no PSRAM, ESP-IDF 5.5.3, LVGL, 240×320 portrait display,
three ADC-ladder buttons and ES8311 audio. Hardware constants belong in
`components/bsp/include/bsp_pins.h`; reusable hardware implementation lives in
`components/bsp`.

`main/CMakeLists.txt` compiles `memo_*` application sources and the two generated
font sources under `assets/fonts/`. The baseline demo pages and `ui_pixel` stay
in the tree for reference and host tests but are outside the application build.
Managed dependencies include `espressif/esp_audio_codec` and `espressif/mdns`;
`dependencies.lock` pins resolution.

## 2. Module layout

| Module | Responsibility |
| --- | --- |
| `memo_app` | `app_main`, input queue, action execution, list and recording pages |
| `memo_state` | Pure transition table and state flags |
| `memo_container` | Pure 80-byte header encoding/decoding, patching, UUIDv4 generation |
| `memo_text`, `memo_time` | Pure UTF-8 and time formatting helpers |
| `memo_recorder`, `memo_player` | Separate capture and playback workers |
| `memo_spool` | SPIFFS mount, files, scanning, finalisation and removal |
| `memo_settings` | NVS values and immediate volume application |
| `memo_net` | Wi-Fi events, reconnect timer, SNTP |
| `memo_discovery` | mDNS lookup and address cache |
| `memo_api` | HTTP probe, list cache, transcript and download helpers |
| `memo_upload` | Upload loop and list polling |
| `memo_events` | SSE subscription and metadata cache updates |
| `memo_provision`, `memo_provision_security` | On-demand BLUFI/NimBLE and security callbacks |
| `memo_ui_detail`, `memo_ui_settings`, `memo_ui_provision` | Lazily constructed pages |
| `memo_ui_common`, `memo_font` | Shared card primitives and font descriptors |

There is no shared `memo_codec` task and no `memo_power` module. List and record
pages are constructed at startup and retained; other pages are created on demand
and retained too.

## 3. Tasks, timers and locking

| Application task | Priority | Stack bytes | Lifetime |
| --- | ---: | ---: | --- |
| `memo_input` | 5 | 4096 | Persistent input/action loop |
| `memo_rec` | 6 | 28672 | Created per recording |
| `memo_play` / `memo_stream` | 5 | 16384 | Created per local/remote playback |
| `memo_upload` | 4 | 8192 | Background upload and polling |
| `memo_events` | 3 | 6144 | Background SSE connection |

Wi-Fi uses ESP-IDF events and a retry timer, rather than a dedicated application
network task. BLUFI starts its own host task when requested. BSP/LVGL and system
tasks are additional allocations, not included in this table.

Button callbacks only map events and enqueue into an eight-element queue. The
input task executes actions; it can wait for recorder/player shutdown and fetch
remote transcripts synchronously. It is not a non-blocking dispatcher.

An arming timer enqueues completion after `MEMO_ARMING_MS=1000`. The input
loop runs the 200 ms recording/progress/list/provisioning heartbeat itself,
including under continuous input. The arming callback only enqueues an event.
Application state and heartbeat actions are owned by the input task; LVGL calls
outside its task use `bsp_lvgl_lock()`. SSE and polling still share the metadata
cache independently.

Stop waits are 3 s for recorder, player and SSE, and 5 s for upload. Recorder,
SSE and upload retain ownership after a timeout: another stop must acknowledge
exit before their task handle is released or a replacement can start. Network
release for provisioning/capture fallback requires both network workers to stop.
SSE uses worker-owned 1 s HTTP reads and upload uses a 3 s socket timeout;
background delays check the stop flag at most every 100 ms. A blocked control
GET can still make upload stop return a timeout, preserving ownership. The input
heartbeat resumes deferred normal workers after their exit acknowledgements arrive.

## 4. State machine and action execution

`memo_state.c` maps `(state, input)` to `(state, action)`. `memo_app.c` executes
the action and sometimes overrides the state. A table transition alone is not
proof that its UI action is implemented.

| Flow | Current execution |
| --- | --- |
| List press → arming | Blocked by any unresolved `.rec`, `.draft`, `.memo`, `.bad` or legacy `.keep`; otherwise show recording page and ring |
| Arming → recording | After one second; early release returns to list |
| Recording → confirm | OK release or five-minute cap; finalise before asking save/cancel |
| Confirm → list | Rename `.draft` to `.memo` or remove draft; default save; errors retain choice |
| Rejected upload → user choice | Three rejected attempts per round; retry/delete on list, default retry; wait for worker acknowledgement |
| Open detail | Force expanded state, even though the table initially returns collapsed |
| Play / pause / resume | Play, stop, restart from the beginning |
| Detail UP/DOWN | Table emits scroll actions; executor has no scroll cases |
| Settings entry/exit | Hold UP or DOWN from list/settings |
| Settings overlay | Changes persist on adjustment; click/hold OK only close it |
| Provisioning exit | UP/DOWN hold emits cancel action; executor has no corresponding case |

A successful provisioning connection changes the provisioning module's state,
but the application has no success-driven teardown/return handoff. The page's
OK-hold exit hint also differs from the state table.

## 5. Startup

`app_main` initialises state and timers, settings, input queue/task, shared I2C,
display/LVGL, spool, fonts/pages, audio, recorder/player, battery, Wi-Fi, uploader,
SSE and finally buttons. Optional peripheral failures are logged and startup
continues where possible; display or input allocation failures return early.

## 6. Container

`memo_container` creates UUIDv4 identifiers and encodes the version-1 MEMO header.
All integer fields are little-endian. An 80-byte header precedes repeated
16-bit-length-prefixed Opus packets. Frame count and duration are patched at
stop; `recorded_at` is UTC seconds or zero before time synchronisation. The
[server container table](server.md#8-upload-container) defines the wire offsets.

## 7. Recording path

```text
ES8311 / I2S RX → bsp_audio_read → Opus encoder → RAM batch → /spool/<UUID>.rec
                  960 samples      60 ms          4096 B
```

The encoder uses 16 kHz mono PCM16, target 20 kbps, VBR, VOIP mode, complexity 0,
FEC off and DTX off. Each iteration reads one 960-sample frame. Encoded frames
have a little-endian 16-bit length prefix. The 4096-byte batch flushes before
overflow or once it reaches 2048 bytes, and on worker exit.

The encoder opens per capture and closes only after the worker acknowledges
stop; idle browsing does not retain its allocation. On `ESP_ERR_NO_MEM`, the
application tries to join SSE and upload before suspending Wi-Fi and retrying;
it restores services after stopping/failure. A stop timeout never releases the
encoder, file or task to a replacement recorder.

The displayed timer and 4:30 warning/5:00 cap use monotonic wall time. Container
duration uses encoded sample count. Each batch write flushes and synchronises
the file. Audio read, encode and append failures stop capture and surface an
interrupted/recovered choice; no failed batch is silently skipped. After joining,
stop patches/synchronises the header and renames `.rec` to `.draft`. Save alone
renames that draft to upload-eligible `.memo`; cancel deletes only the draft.
Save/cancel I/O failures leave the choice visible for another attempt.

## 8. Playback path

Local playback reads a MEMO file; remote playback reads
`GET /api/v1/memos/{id}/audio` directly. Both parse the 80-byte header, decode
length-prefixed Opus packets on the device and send PCM through `bsp_audio_write`.
The remote stream is the original container, not WAV, and is not cached to Flash.

The decoder opens on demand and closes at playback end. PCM output buffer size
is 8192 bytes; local packet capacity is 2048 bytes and remote capacity is
512 bytes. Remote HTTP timeout is 20 s with a 2048-byte client buffer. There is
no separate playback ring/prebuffer or Range-based continuation. The pause action
stops the task; resume launches playback from byte zero.

## 9. Local spool and partitions

| Partition | Offset | Size |
| --- | ---: | ---: |
| `nvs` | `0x9000` | `0x6000` |
| `phy_init` | `0xf000` | `0x1000` |
| `factory` | `0x10000` | `0x6f0000` |
| `memo` (SPIFFS) | `0x700000` | `0x100000` |

The 1 MiB spool is transient storage intended for one five-minute recording
(roughly 0.75 MB at the target bitrate, excluding filesystem overhead).
`CONFIG_SPIFFS_OBJ_NAME_LEN=48` accommodates UUID filenames. Mount uses
`format_if_mount_failed=true` and allows four open files. A failed mount can
therefore cause formatting; preservation of prior recordings is not guaranteed.

Names are the index: `.rec` is being written, `.draft` awaits explicit save,
`.memo` is pending, `.bad` awaits retry/delete, `.keep` is compatibility input,
and storage helpers support `.sent`. No new `.keep` archive is created. Accepted
uploads delete `.memo` directly rather than producing `.sent`.

Before uploader startup, recovery scans packet boundaries in `.rec`, `.draft`
and `.memo` files. Complete packets survive interruption; a partial trailing
packet is truncated, frame count/duration are rebuilt and the file becomes an
unconfirmed draft. Intact confirmed `.memo` stays pending. Invalid/zero-packet
files are quarantined as `.bad`, retaining bytes for a user decision. Recovery
is idempotent; it does not decode Opus or recover an unflushed RAM batch.

Scanning reads at most 16 entries in filesystem order. Pending/rejected/legacy
rows precede server cache entries, using header time/duration and matching local
playback extensions. `.rec` and `.draft` occupy the processing slot too; the input
loop restores draft save/cancel before rejected-upload review. Every unresolved
file blocks new capture. Existing legacy backlogs are drained sequentially.

The mutex-protected runtime capture hold complements these persistent states.
The uploader checks both before work and after blocking discovery/probing, so a
reboot cannot turn an unconfirmed draft into an upload. Failed recovery I/O
shows a storage error and holds upload/new capture until recovery succeeds;
state renames never replace existing files.

## 10. Networking

### 10.1 Wi-Fi and provisioning

Stored Wi-Fi credentials are read through ESP-IDF. Reconnect starts at 2 s and
doubles to 60 s. SNTP starts after acquiring an IP. `memo_net_now()` returns zero
until a plausible epoch (2020 or later) exists. SSID and IP are logged; passwords
are not explicitly logged by this module.

Provisioning advertises `BLUFI_MEMO` and uses BLUFI security callbacks. Entering
requests SSE/upload stop, releases mDNS and suspends the normal Wi-Fi owner.
`memo_provision_stop()` provides cleanup, but is not called by the application
exit path. A successful provisioning IP event does not resume normal workers.

### 10.2 Discovery and HTTP

mDNS queries `_voicememo._tcp`: IPv4 from PTR results, TXT `ip=` fallback, then
SRV/A resolution or hostname fallback. Finding an address sets discovery to
`FOUND`; that state alone does not prove HTTP reachability. The upload worker's
initial probe accepts `/ping` 200 or 204, then reads `/server` limits when
available. A failed `/server` response after successful ping still returns probe
success. The cached duration limit is checked before upload; the byte limit is
logged, not enforced locally.

Control GETs use a 15 s timeout and 2048-byte HTTP buffer. Upload uses 3 s and
4096 bytes, streaming file chunks of 1024 bytes with `X-Device-Id` (MAC-derived)
and `X-Memo-Id` (UUID). HTTP 200/201 deletes the pending file. The pure
`memo_upload_policy` saturates after three HTTP 4xx rejections total (including
429), then renames to `.bad` and stops POSTs. A known server-duration precheck
uses the same three-round rejection budget without sending oversized data.
Transport/discovery/5xx failures do not consume that budget. Failed attempts
back off from 5 s exponentially to 120 s; normal first/second rejections wait
5 s/10 s. Counts are per ID in RAM; `.bad` persists across restart.

The upload worker scans `.bad` even offline. `memo_upload_rejection()` copies a
mutex-protected snapshot; `memo_upload_choose()` queues an ID-checked choice
without I/O. The worker removes the reviewed file only for delete. Retry renames
`.bad` or legacy `.keep` to `.memo` with the same ID, clears the rejection counter
and backoff, and starts a fresh three-attempt round. This does not release the
capture guard. A pending retry has priority over reviewing other legacy files;
exhausting it publishes that same recording for review immediately. The snapshot
includes recording time/duration and the legacy source state.
Errors preserve the review and allow another choice. A failed terminal rename
never causes a fourth HTTP request in the round. The input task polls every
200 ms independently of the stoppable UI tick, opens `UPLOAD_REJECTED` from LIST,
defaults to retry and waits for worker acknowledgement before closing. Closing
restarts the list tick so a later successful retry can refresh visible metadata. Existing discovery addresses are reused
until discovery is reset, rather than invalidated on every connection failure.

### 10.3 List cache and SSE

The client fetches `/memos?limit=24` into a 24-entry RAM cache without following
`next_cursor` or filtering by device. Failed refresh keeps the previous cache.
The list renders only five fixed cards and has no viewport/pagination logic.
Remote transcript copies use a 512-byte destination in `memo_app`.

`memo_upload` polls every 2 s in the hot period after upload and about every
60 s otherwise. SSE uses `/events`, a 1024-byte HTTP buffer and 512-byte
line/payload storage; events upsert metadata and mark the cache updated. It
reconnects after 5 s when a subscription ends. This updates cached metadata;
visible list refresh also depends on the application timer running.

## 11. Settings and power

NVS namespace `memo` stores keys `vol`, `to`, `sl`, `ap` and `tz`. Defaults are
volume 80%, timeout 30 s, sleep 5 min, autoplay off and timezone UTC+8.
Volume options are 0/20/40/60/80/100%; timeout 0/30/60/180 s; sleep 0/1/5/15 min.
Each change attempts an immediate commit; NVS write/commit errors are not
propagated to the caller. Volume also calls `bsp_audio_set_volume`.
There is no preview tone, edit snapshot or rollback on cancel.

Backlight is set to 100 at startup. Timeout/sleep values have no application
power-control loop. Boot checks GPIO wake and arms the existing key-release
guard, then starts at the list; this does not establish a working automatic
sleep/wake feature. NVS initialisation may erase NVS on the ESP-IDF no-free-pages
or new-version errors, affecting settings and stored Wi-Fi credentials.

## 12. UI and fonts

The [visual language](../design/visual-language.md) describes the current cards
and layout. `memo_fonts_init()` copies generated font descriptors and attaches
Montserrat fallback: 20 px body to Montserrat 20, 16 px labels to Montserrat 14.
Generated font data lives under `assets/fonts/`; LVGL uses the system heap and
large font tables via tracked defaults. `memo_font_check_text()` logs missing
code points for detail text; it is not a complete host-side coverage suite.

Only the list constructs a battery label. There is no shared per-page battery
status bar. UTF-8 helpers have host tests, but current `snprintf` copies and
unbounded row labels do not guarantee character-safe clipping throughout the UI.

## 13. Validation interfaces

`tools/validate.sh --static` includes `test_memo_state`, `test_memo_text`,
`test_memo_time` and `test_memo_container`, plus inherited BSP/demo and tooling
tests. `test_memo_recorder` exercises worker failures, stop timeout, wall time and encoder
ownership; `test_memo_spool` covers real-file recovery and draft commit/cancel.
`test_memo_events` checks acknowledged stop, idle reads and connection cleanup.
`test_memo_upload` executes the real worker logic with injected HTTP/file
failures, covering the three-attempt boundary, no early deletion, retry/delete
acknowledgement, renewed three-attempt rounds, slot blocking through retry,
legacy-file drain, offline review recovery and capture/probe holds.
`test_memo_spool` exercises real temporary-file retry/legacy rename/scan/content preservation.
`test_memo_rejection_layout` verifies font-metric spacing and display bounds.
`test_memo_rejection_font.py` checks the dialog strings against both generated
font cmaps with a known-missing negative case. State tests cover dialog defaults
and controls; they do not establish actual LVGL rendering or full app handoff.

The full gate builds from tracked defaults, verifies the merged image and
archives matching debug artifacts. On macOS, the Linux wrapper runs host tests
with GNU linker support. Server tests are a separate Go command. Actual runs
and hardware results belong in the [milestones](../project/milestones.md).
