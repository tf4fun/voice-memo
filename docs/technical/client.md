<p align="right">
  <a href="client.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Voice Memo Client — Technical Design

Implementation snapshot: 2026-11-09. This document describes the compiled
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
| `memo_settings` | Validated NVS values, draft editing and confirmed volume application |
| `memo_net` | Wi-Fi events, reconnect timer, SNTP |
| `memo_discovery` | mDNS lookup and address cache |
| `memo_api` | HTTP probe, paginated list cache and transcript windows |
| `memo_upload` | Upload loop and list polling |
| `memo_events`, `memo_sse` | SSE subscription, bounded framing and snapshot refresh requests |
| `memo_handoff` | Pure provisioning/normal-network ownership coordinator |
| `memo_power` | Idle/first-wake-gesture policy and ordered terminal shutdown |
| `memo_provision`, `memo_provision_security` | On-demand BLUFI/NimBLE and security callbacks |
| `memo_ui_detail`, `memo_ui_settings`, `memo_ui_provision` | Lazily constructed pages |
| `memo_ui_common`, `memo_font` | Shared card primitives and font descriptors |

The pure `memo_power` policy/coordinator is driven by the application input task.
There is no shared `memo_codec` task. Only the current functional page exists.
Transitions switch to the fixed empty boot screen, unregister battery labels,
stop animations, delete the old screen/children and clear widget pointers before
constructing the destination. List/arming/record, detail, settings and provisioning
all follow this lifetime. A missed LVGL lock retries from application state;
settings drafts, list selection and detail data survive widget deletion.

## 3. Tasks, timers and locking

| Application task | Priority | Stack bytes | Lifetime |
| --- | ---: | ---: | --- |
| `memo_input` | 5 | 8192 | Persistent input/action loop |
| `memo_rec` | 6 | 28672 | Created per recording |
| `memo_play` / `memo_stream` | 5 | 16384 | Created per local/remote playback |
| `memo_upload` | 4 | 8192 | Background upload and polling |
| `memo_events` | 3 | 6144 | Background SSE connection |

Wi-Fi uses ESP-IDF events and a retry timer, rather than a dedicated application
network task. BLUFI starts its own host task when requested. BSP/LVGL and system
tasks are additional allocations, not included in this table.

Button callbacks timestamp/map events and enqueue into an eight-element queue. The
input task executes actions and can wait for recorder/player shutdown or storage.
Remote text/page reads are queued to the uploader. It is not a fully non-blocking
dispatcher.

An arming timer enqueues completion at `MEMO_ARMING_MS=1000` after the
press timestamp, subtracting dispatch delay. The input
loop runs the 200 ms recording/progress/list/provisioning heartbeat itself,
including under continuous input. The arming callback only enqueues an event.
Application state and heartbeat actions are owned by the input task; LVGL calls
outside its task use `bsp_lvgl_lock()`. The uploader owns cache replacement;
SSE submits coalesced refresh requests. Metadata reads copy under a mutex.

Stop waits are 3 s for recorder, player and SSE, and 5 s for upload. Recorder,
player, SSE and upload retain ownership after a timeout: another stop must acknowledge
exit before their task handle is released or a replacement can start. Network
release for provisioning/capture fallback requires both network workers to stop.
SSE uses worker-owned 1 s HTTP reads and upload uses a 3 s socket timeout;
background delays check the stop flag at most every 100 ms. A blocked control
GET can still make upload stop return a timeout, preserving ownership. The input
heartbeat resumes deferred normal workers after their exit acknowledgements arrive.
After acknowledgement, workers park and their owner deletes them (including
playback polling after natural completion). This reclaims stack/TCB memory in
the joining call instead of waiting for idle-task self-deletion cleanup. Timeout
never authorises deletion of a worker still accessing audio, files or HTTP.

## 4. State machine and action execution

`memo_state.c` maps `(state, input)` to `(state, action)`. `memo_app.c` executes
the action and sometimes overrides the state. A table transition alone is not
proof that its UI action is implemented.

| Flow | Current execution |
| --- | --- |
| List press → arming | Blocked by any unresolved `.rec`, `.draft`, `.memo`, `.bad` or legacy `.keep`; otherwise keep the list for short taps, then show a separate preparation page after 180 ms |
| Arming → recording | At least one second from press and resources ready; release returns to list and a committed gesture consumes its trailing click |
| Recording → confirm/undo | OK release or five-minute cap; finalise before manual choice or optional autosave undo |
| Confirm → list | Rename `.draft` to `.memo` or remove draft; default save; errors retain choice |
| Rejected upload → user choice | Three rejected attempts per round; retry/delete on list, default retry; wait for worker acknowledgement |
| Open detail | Full text panel and queued text read; autoplay enters playing |
| Play / pause / resume | Play, pause in place, continue the same source |
| Detail UP/DOWN click | Apply/save volume in 20% steps, clamped at 0/100%; keep memo and scroll selection |
| Detail UP/DOWN hold | Safely join the old audio source, then switch previous/next; queue page reads without leaving detail |
| Settings entry/exit | Hold UP or DOWN from list/settings |
| Settings overlay | Adjust a draft; click OK saves, hold cancels; failed saves retain the overlay |
| Provisioning exit | UP/DOWN hold requests cleanup and return to settings; success/fatal failure also hands back ownership |

After an IP has been held for two seconds, provisioning tears down BLE/Wi-Fi
and resumes normal Wi-Fi, discovery, upload and events. Fatal startup/module
failure takes the same cleanup path. Failed cleanup retains ownership and retries;
normal Wi-Fi is not restarted until cleanup succeeds. UP/DOWN hold cancels through
this coordinator. Provisioning retains its context footer without a key legend.

## 5. Startup

`app_main` initialises state/cache and timers, settings, input queue/task, shared I2C,
display/LVGL, spool, audio, recorder/player, battery, Wi-Fi/discovery, uploader,
SSE and buttons before constructing fonts/the initial list page. Long-lived
resources precede short-lived widgets. Optional peripheral failures are logged and startup
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
stop; idle browsing does not retain its allocation. Every capture first joins
SSE/upload behind the animated preparation page, then allocates audio; an
unacknowledged worker aborts capture without deinitialising its network. A second
physical OK check plus a callback-side release epoch cancels even a queued
release/repress during joining or codec/file preparation. On `ESP_ERR_NO_MEM`, the
application suspends Wi-Fi and retries. Services resume after save/cancel or a
failure, remaining stopped during confirmation/undo. A stop timeout never releases the
encoder, file or task to a replacement recorder.

Startup allocates the 28672-byte contiguous worker stack before opening the
approximately 24640-byte encoder. The worker waits at a start semaphore until
the encoder, PCM/output buffers and spool file are ready. `memo_recorder_prepare`
leaves the worker gated and its timer at zero; `memo_recorder_begin` releases it
without reallocating. `memo_recorder_cancel_prepared` deletes the parked worker,
closes the encoder/buffers and discards the empty file. Spool discard propagates
close/remove errors instead of reporting failed removal as success. Failed preparation
deletes the parked worker from its owner, immediately releasing its stack, and
closes/frees its encoder and buffers before retrying. This avoids splitting the
largest free block with encoder allocations before requesting the stack; total
free heap alone is not evidence that a task can start. No stack is reserved
while idle or playing. SNTP starts once per boot, independently of whether time
has synchronised, so Wi-Fi suspension/reconnection cannot reconfigure a running
SNTP client.

The displayed timer and 4:30 warning/5:00 cap use monotonic wall time. Container
duration uses encoded sample count. The worker also enforces 5000 frames,
so 60 ms packet audio cannot exceed 300000 ms even if the heartbeat is delayed. Each batch write flushes and synchronises
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
512 bytes. Remote HTTP timeout is 2 s with a 2048-byte client buffer. There is
no separate playback ring/prebuffer or Range-based reconnection. Pause retains
the worker, decoder, source and progress, waiting at packet/output boundaries;
resume continues the same source. A current I2S write may finish after the pause
request. Loss of a paused remote connection ends playback rather than replaying
it. Stop waits up to 3 s; timeout retains task ownership, blocking another worker
and capture. Completion is joined before a new worker; old acknowledgements
cannot release a newer playback task.

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
`format_if_mount_failed=false` and allows four open files. Only an entirely
erased partition, verified by reading every byte, is automatically formatted on
first use after a failed mount. Existing bytes, a missing partition or read error
block capture/upload and preserve data. NVS initialisation errors likewise do
not erase settings or Wi-Fi credentials.

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

Scanning counts all recognised files and copies at most 16 entries, prioritising
capture/draft, pending, rejected/legacy and finally acknowledged compatibility
files; filesystem order is retained within each priority. An old archive cannot
hide the active recording from bounded readers. A directory open/read/close
failure latches the recovery guard; capture creation and uploader checks honour
it immediately, including a failure discovered by the current scan. Pending/rejected/legacy
rows precede server cache entries, using header time/duration and matching local
playback extensions. A matching remote UUID is omitted while that local source
is retained, so an accepted upload with an active reader remains one row. `.rec` and `.draft` occupy the processing slot too; the input
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
until a plausible epoch (2020 or later) exists. Startup checks event registration
and credential-read errors; retry registers only the missing handler and reuses
the initialised driver. Repeated start preserves a live connection, late retry
callbacks do not reconnect it, and an IP event during driver startup is not
overwritten by a subsequent connecting-state assignment.

Wi-Fi and BLUFI SSID/IP display data is copied under short critical sections;
no mutable string pointers escape to the UI. Credential changes clear the old
address, and bounded copies terminate a full 32-byte SSID and clear suffixes
when a shorter network replaces it. BLUFI reports also copy driver configuration
before sending, with driver and BLE operations outside the critical section.
Only successful driver writes publish credentials to that snapshot; a revision
check prevents a delayed STA configuration read from overwriting a newer phone
update. The UI never reads passwords.

Provisioning advertises `BLUFI_MEMO` and uses BLUFI security callbacks.
`memo_handoff` joins SSE/upload before releasing mDNS and normal Wi-Fi. Failed
entry rolls back; failed shutdown retains resources for a later acknowledgement.
Normal Wi-Fi events are ignored while suspended, including when the previous
normal session had no credentials. Event publication checks its session after
formatting, so an in-flight old callback cannot restore an address after suspend
or a new session. Module failure/cancel/success return through
the same coordinator. Normal Wi-Fi suspend returns stop/deinit failures and retains
ownership; a subsequent start retries unfinished release before allocating a new
driver. Provisioning and low-memory capture never acquire it after a failed
release. BLUFI teardown joins the host before freeing GATT, checks port release,
waits for profile-deinit acknowledgement before freeing BTC, and preserves failed
Wi-Fi/handler cleanup steps for retry. Driver cleanup follows host-stop acknowledgement; NimBLE
stop itself may block inside its platform implementation. After the recorder
borrows network memory, both successful and failed stop paths restore the entire
Wi-Fi/discovery/upload/SSE pipeline. Failed STA or worker starts remain queued
for heartbeat retry; a failed STA state is retried as well as an off state.

### 10.2 Discovery and HTTP

mDNS queries `_voicememo._tcp` using PTR, TXT `ip=`, then SRV/A or hostname
fallback. Queries/teardown are serialised separately from short endpoint-copy
locks. Discovery returns a candidate; only a valid API-v1 `/ping` response and
`/server` limits confirm it. Endpoints include a generation: a late failure from
an old address cannot invalidate its replacement. Transport/5xx failure revokes
the affected endpoint and subsequent work discovers/probes again. The settings
server action queues a coalesced request for this same uploader worker; it keeps
waiting while offline or capture/review owns resources. An online request wakes
network backoff, invalidates the copied endpoint and re-verifies before refresh
or upload, retaining the existing metadata cache on failure. A request received
during an attempt is processed in the next worker cycle.

Control GETs use a 3 s socket timeout and 2048-byte buffer, with a 32 KiB body
cap; callback allocation/size errors prevent parsing or cache replacement. Both
server duration and byte limits apply before upload. Upload uses a 3 s socket
timeout and 4096-byte HTTP buffer, streaming 1024-byte chunks with MAC-derived
`X-Device-Id` and UUID `X-Memo-Id`. POST requires a confirmed endpoint. HTTP
200/201 removes the pending file. Three HTTP 4xx rejections, including 429, or
known limit-precheck failures exhaust a round and rename to `.bad`. Transport,
discovery and 5xx failures retain the file without consuming that budget.
Backoff remains 5 s to 120 s; counters are per ID in RAM, `.bad` persists.


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
200 ms on the input loop, opens `UPLOAD_REJECTED` from LIST, defaults to retry
and waits for worker acknowledgement before closing. The persistent heartbeat
continues to refresh visible metadata after a successful retry.

### 10.3 List cache and SSE

`memo_api` requests and retains one page of at most four records in RAM. The API
cache, input snapshot and list model each limit server metadata to four records; an oversized
response is rejected without replacing the current page. Detail navigation loads
the adjacent four-record page only at its boundary. Local spool recovery entries
remain separate from this server-page bound. `memo_api_request_page()` queues older,
newer or latest requests for the uploader, using URL-safe next/previous cursors.
Rows and cursor metadata can be copied under one lock. A serial invalidates old
in-flight responses; failed moves retain the displayed page/query and report an
error. Successful explicit navigation increments the committed page generation;
background refresh preserves its anchor. Status buffers hold `transcribing`
without truncation, and numeric/cursor/ID/status validation precedes replacement.

The text-reader API queues a 2048-byte UTF-8 window for the uploader. Requests
carry byte offset, direction and optional SHA-256 revision. Copy reads are locked;
cancel/new selection invalidates late responses. Transport/5xx failures retain
the old window and retry; malformed/version/missing-record responses stop for
the caller to handle. Bounds, text length, strict UTF-8, ID, status and revision
must agree before publication. No transcript window is persisted to Flash.

The executor copies page rows/cursors together into its list model, preserving
selected ID on background refresh and choosing a visible edge after explicit
navigation. Four two-line row widgets render a moving viewport over local entries and
the server page. Detail uses one full-height text mode with play/pause/resume.
A pure proportional policy maps audio position to a transcript byte target,
then the rendered window to its scroll extent. Requests use only server-provided
UTF-8 boundaries, never the interpolated byte target. Pausing freezes following;
replaying requests byte zero when needed. No timed transcript alignment is assumed. A detail
keeps its own entry snapshot. New selection/back cancels old text requests;
revision conflict restarts at byte zero. Obsolete full-transcript/download helpers
and the unused Flash playback cache path have been removed. List-page errors do not affect detail reads. Failed detail reads retry after
a bounded delay while retaining existing text. Metadata is
not filtered by device.

`reconcile_ui` retries rendering from application state after LVGL lock timeout;
saves/network/audio actions are not replayed. Settings selection, draft and error
are application-owned. Blocked-capture/audio-unavailable feedback is stored in
a three-second notice and survives render retries. Battery SOC is sampled outside LVGL every five seconds
and copied into registered labels for list/record/detail/settings/provisioning.
Local playback holds the upload source until player join. If POST was already
in flight, success retains the file and a later idempotent request releases it.
The detail follows the same ID remotely after the transient file disappears.

`memo_upload` polls every 2 s after upload and about every 60 s otherwise. SSE
uses a 1024-byte HTTP buffer and pure `memo_sse` framing with 512-byte line/data
caps. Fragmented/multiline events are supported; overflow drops the whole event
and requests a fresh list. Parser state resets on reconnect. Every event,
including deletion or empty edited text, and every successful reconnect requests
a coalesced authoritative list refresh. The SSE task never merges partial cache
rows. Failed refresh preserves the last complete cache; reads return copies
under the cache mutex. Subscriptions retry after 5 s.
When a rejected-upload choice finishes, its panel/labels are deleted and the list
rows are restored in that render; a hidden dialog is not retained until navigation.
Recorder discard propagates spool cleanup failure after releasing its audio resources.

## 11. Settings and power

NVS namespace `memo` stores keys `vol`, `br`, `to`, `sl`, `ap`, `as` and `tz`. Defaults are
volume 80%, brightness 100%, timeout 30 s, sleep 5 min, autoplay/autosave off and timezone UTC+8.
Volume options are 0/20/40/60/80/100%; timeout 0/30/60/180 s; sleep 0/1/5/15 min.
Integer values are validated before narrowing; unsupported options use defaults
without rewriting legacy keys. Timezone is bounded to UTC-12 through UTC+14.
Initialisation/open failure returns an error and preserves NVS. Setters return
write/commit errors and publish runtime values only after success; volume then
calls `bsp_audio_set_volume`. Failure attempts to restore the last confirmed key,
but persistent Flash errors cannot guarantee rollback or unchanged storage.

`memo_settings_edit` drafts snapshot the confirmed settings. Begin/adjust/cancel
have no NVS/audio effects. Save writes only the selected field; failure retains
the draft for retry/cancel, leaving confirmed runtime settings intact. The input
task performs saves and autoplay toggles outside the LVGL lock, then renders the
result. A failure hint keeps the value overlay open. Volume applies on save;
there is no preview tone. Active editing keeps the display on and blocks sleep.

Brightness ranges from 10% to 100% in clamped 10% steps; UP increases and DOWN
decreases. Its editor shares the animated volume meter. The input owner applies
draft PWM immediately outside the LVGL lock without writing NVS or changing the
confirmed setting. Cancel restores the saved level; failed saves retain the
preview/draft for retry or cancel. Missing/invalid legacy `br` values use 100%
without rewriting storage. No new PWM driver, task or timer is added.

Startup and wake apply the confirmed brightness; 0% remains reserved for blanking.
Unchanged heartbeats do not rewrite PWM. The input heartbeat first applies the idle screen
timeout, then starts the deep-sleep delay at the actual blank transition.
`blank_since_ms` is independent of the last activity timestamp. A delayed first
poll cannot skip the display-off interval. Always-on display disables automatic
sleep; zero sleep delay disables sleep alone. Physical
key events reset idle time. An ordinary wake gesture restores the backlight and consumes the trailing
click; an intentional held OK may continue into arming. Recording, arming,
confirmation/rejection decisions, active or paused playback, provisioning and
held/unreadable keys keep the screen on and restart the idle interval. Unconfirmed
drafts, live capture files, recovery blocks, live audio ownership and network
recovery prevent deep sleep; closed pending files do not.

Shutdown requests SSE/upload/player stop, requires acknowledgements, rechecks
spool/input safety and acquires the LVGL lock. Preflight failure recovers workers
and retries after a new idle interval. It arms GPIO wake before terminal teardown,
releases discovery/Wi-Fi and buttons, then suspends the battery gauge, codec,
I2S pins, shared I2C and display in order. Terminal button failure restarts;
peripheral errors are logged while the remaining shutdown steps continue. Any
return from deep sleep restarts. GPIO wake uses the BSP key pin; boot returns to
the list, allowing a still-held OK to arm once startup is ready while
suppressing ordinary wake gestures until a fresh press. NVS init errors
preserve saved settings and credentials.

## 12. UI and fonts

The [visual language](../design/visual-language.md) describes the current cards
and layout. `memo_fonts_init()` copies generated font descriptors and attaches
Montserrat fallback: 20 px body to Montserrat 20, 16 px labels to Montserrat 14.
Generated font data lives under `assets/fonts/`; LVGL uses the system heap and
large font tables via tracked defaults. The [assets manifest and license records](../../assets/README.md#voice-memo-fonts)
pin Noto Sans SC 2.004, source hashes, OFL 1.1 and converter 1.5.2. The
reproduction tool verifies bitmap/metric/cmap payloads without changing C assets.
Static UI literals are checked at both sizes. Reapplying identical filtered
text does not restart label marquee. Provisioning gives SSID a separate single
scrolling line, leaving the address below it even with a full-length name. The emitted sets contain 4012
code points, including 3755 GB2312 level-one Han, ASCII and punctuation; unassigned
U+FF00 is omitted. Dynamic unsupported characters/invalid byte sequences display
`?` through the selected font/fallback, without changing server text or offsets.

Actual line heights are 31/38 px. Headers, row/overlay geometry and footers use
these metrics; record confirmation uses Chinese choices in the CJK body font.
List/settings labels have fixed widths and ellipsis/marquee behaviour. All pages
register a shared cached battery label. Host object/geometry checks are not a
claim of pixel rendering or measured memory on hardware.

## 13. Validation interfaces

`tools/validate.sh --static` includes `test_memo_state`, `test_memo_text`,
`test_memo_time` and `test_memo_container`, plus inherited BSP/demo and tooling
tests. `test_memo_recorder` exercises worker failures, stop timeout, wall time and encoder
ownership; `test_memo_spool` covers real-file recovery and draft commit/cancel.
`test_memo_events` checks acknowledged stop, idle reads and connection cleanup.
`test_memo_browse` checks selected-row visibility through 80 records, page-edge
decisions and content-row bounds. `test_memo_ui` executes actual common/font/
settings/detail/provisioning modules against a host object/scroll model, covering all nine
settings, draft display, full detail/play/pause modes, long text scrolling,
unsupported/malformed glyph input, network/discovery values and shared battery. It is not a raster test or
an execution of the complete application input task. `test_memo_network_info`
checks concurrent bounded snapshots and shorter/full-length SSIDs;
`test_memo_net` injects actual STA start/disconnect/IP/suspend/restart events.
`test_memo_notice` checks feedback lifetime without render consumption, and
`test_memo_upload` exercises manual discovery queuing/coalescing, offline/busy
retention, request-during-query and retry-backoff interruption.
`test_memo_provision` executes actual BLUFI lifecycle code with late host/profile
acknowledgements, stop/deinit/handler faults, partial startup, malformed credential
lengths and an interleaved old-configuration/new-SSID update. The STA fixture
covers stop/deinit ownership and stale callbacks across suspend/restart. Spool
checks include acknowledged-file overflow and directory-read failure. Application
boot variants cover held and already-released deep-wake keys; the guard publishes
its deadline before atomically arming.

`test_memo_app` includes the actual application, boots `app_main` and runs bounded
input-loop segments with injected platform/worker boundaries, real state/settings
code and all real UI modules. It covers every action reachable from normal boot:
page controls, text-window/revision/cancel, play/pause/resume/autoplay, save/cancel
and failed commit, recording/draft/rejection flows, local-reader UUID overlap,
provisioning stop/rejoin and wake-gesture consumption. Lock assertions cover UI
access; effect counters verify render retries do not repeat writes or starts.
Continuous input-loop segments also check the 200 ms heartbeat. The
page-lifetime regression repeats 100 detail/settings/provisioning/capture cycles,
checks a constant live-widget count after return, battery updates after deletion,
network join failures and release of OK during joining. No recording start may
overlap a live upload/SSE worker.
Every remaining action is covered; unreachable legacy error states/action hooks
have been removed. This harness does not run the RTOS scheduler,
physical drivers, real worker tasks concurrently or a raster renderer.

`test_memo_player` runs the actual local/fragmented-HTTP workers, preserving frame
order through pause/resume and exercising stop timeout, late completion and stale
acknowledgements. `test_memo_power` checks idle boundaries, protected activity,
full wake-gesture consumption and shutdown ordering/failure recovery.
`test_memo_settings` executes real draft/setter code with NVS fault injection,
including failed rollback, retry, reboot reload, legacy-value validation and
preservation of unrelated fields. The font test also covers the settings failure hint.
`test_memo_api`, `test_memo_discovery`, `test_memo_sse` and `test_memo_handoff`
cover response limits/cache ownership, endpoint generations, parser boundaries
and provisioning rollback/acknowledged restart.
API coverage also includes bidirectional page requests, whole-page snapshots,
failed/stale moves, bounded/versioned text, retry versus semantic errors and
cancel/reselection during HTTP. UTF-8 validation rejects overlong encodings,
surrogates and code points above U+10FFFF.
`test_memo_upload` executes the real worker logic with injected HTTP/file
failures, covering the three-attempt boundary, no early deletion, retry/delete
acknowledgement, renewed three-attempt rounds, slot blocking through retry,
legacy-file drain, offline review recovery and capture/probe holds.
`test_memo_spool` exercises real temporary-file retry/legacy rename/scan/content preservation.
`test_memo_rejection_layout` verifies font-metric spacing and display bounds.
`test_memo_rejection_font.py` checks all static UI strings against both generated
font cmaps, fixture line heights, provenance/payload hashes and the complete
GB2312 level-one inventory, with a known-missing negative case. State tests cover dialog defaults
and controls; they do not establish actual LVGL rendering or full app handoff.

The full gate builds from tracked defaults, verifies the merged image and
archives matching debug artifacts. Host runtime fixtures use native linker
dead-code removal on macOS and Linux; the Linux wrapper remains an optional CI
environment check. Server tests are a separate Go command. Actual runs
and hardware results belong in the [milestones](../project/milestones.md).


### Inspiration capture refinements

Optional autosave uses a durable closed draft and an application-owned three-second
undo state, starting at the first successful rendering lock. Upload remains held
until the normal draft-to-pending transition succeeds. Failures and recovered
drafts use manual confirmation. The persisted `as` boolean defaults off. Settings
now contain eight rows. Wake-held OK is forwarded as intent by the queue-only
callback and rechecked against the BSP ADC window before capture; ordinary wake
clicks and direction keys remain consumed. PCM hints are computed per frame and
published atomically; they do not auto-delete quiet audio or modify gain.

Closed pending files no longer block idle deep sleep. Capture, draft, recovery
errors and undo do. Shutdown joins network workers before a bounded priority scan
for drafts/live files. The input owner retains spool write ownership, allowing the
locked safety recheck to reuse the scan without filesystem I/O under the LVGL
lock. No periodic wake is added; manual wake restores the existing network flow.
Actual wake latency, current and microphone thresholds require board acceptance.


### Bounded terminal columns

The shared ASCII font now uses a reproducible, vertically doubled Unscii-8
bitmap with an 8 px fixed advance and 18 px line height. The bundled Unscii-16
actually advances 16 px; the earlier 8 px assumption caused overlapping list
timestamps and status text on hardware despite object-model checks. Each 208 px
row now reserves a 184×31 summary at x=12/y=0, then independent 40×18 time and
duration fields at x=12/156, y=31. Numeric fields clip; unselected long text uses
ellipsis in its own vertical band. The
shared font also restores the intended widths of battery and playback labels.
The static gate regenerates the font from pinned local source, checks actual
advances and fixture metrics, and asserts production column bounds.


### Settings marquee lifetime

Settings value labels have a fixed one-font-line height as well as an 88 px
width. LVGL's dots mode needs a height limit to truncate an unselected long
address; content height would allow it to wrap. Selected values use the same
single-line bounds while scrolling.

The pinned LVGL long-mode setter deletes the label animation and clears its
offset even when passed the current mode. Settings refresh now reads the current
mode and calls the setter only for selection transitions. Identical filtered
text is already retained. Hidden outgoing rows also switch to ellipsis, so a
new selection starts normally while heartbeat renders do not restart scrolling.
Host fixtures model setter resets and assert continuity across repeated values
refreshes and actual input-loop heartbeats; pixel speed remains a device check.

The memo list uses the same single-line circular marquee only on the selected
row; unselected or hidden rows use ellipsis and stop their animation. Preview
line breaks and tabs become spaces. A 37-byte selected UUID cache restarts the
marquee when a row is rebound to a different memo, even if its text is identical.
Heartbeat and unchanged server refreshes preserve the current animation. The
page displays at most four recording cards, each with one transcript line. Unselected rows keep their
bounded previews. A selected potentially truncated preview, after a 400 ms dwell,
requests the full transcript through the existing text worker. List and detail
share the input owner's 2 KiB text snapshot rather than caching every transcript.
Long text advances through revisioned UTF-8 windows after each marquee pass; a
short final window dwells for three seconds before returning to the beginning.
The LVGL completion callback only signals an epoch; network work stays outside
the LVGL lock. Changing selection cancels old requests, entering detail explicitly
hands over ownership, and late replies cannot replace the new selection.
Page teardown clears display epochs and deletes the label animations with their
widgets. No new task or independent timer is added.

The selected label uses an explicit duration derived from its actual text width
and the font's circular-scroll gap at 40 px/s. This avoids LVGL's encoded-speed
10-second maximum lap duration, which would otherwise accelerate long windows.

List rows are reapplied after changed metadata, selected-text windows, selection
changes or page rebuilds, rather than on each 200 ms input heartbeat or unchanged
one-second model snapshot. Unchanged time/duration/status text
and battery labels are not rewritten, and status styles are updated
only when their state flags change. Failed text allocation leaves the rows dirty
for a later retry. This reduces allocation and partial-render work while the
selected label animates; the input/network heartbeat and display refresh period
remain unchanged. Synthetic LVGL workload measurements do not establish the
device's frame rate or perceived smoothness.

The list and settings pages use the area previously reserved for key legends.
List cards have equal 16 px outer margins and 12 px inner text insets. The
duration is right-aligned within its 40 px field, so a short value such as
`0:12` does not leave an extra character's blank space on the right.
Record position appears in the header beside the battery. Four list cards span
y=52..294; settings shows six of its nine entries across y=52..296, scrolling
the viewport to keep the selection visible. These bounds retain the rounded
panel's safe insets. Save/error notices temporarily overlay the bottom content;
an empty notice stays hidden and reserves no footer space. Settings also shows
its selected position in the header and stops background marquees while editing.


### Capture gesture and live input presentation

`memo_capture.h` owns the pure 180 ms tap/preparation boundary and click-tail
policy. Physical events carry callback timestamps. Input dispatch updates the
gesture before reconciliation, waits for queued input to drain before showing
preparation, and consumes a committed gesture's click until a fresh OK press.
Arming completion checks the current one-second deadline, so a stale queued
timer cannot complete a newer hold early. The physical ADC guard remains in
place before capture. Wake guards retain their existing full-gesture semantics.

A separate preparation screen owns the ring; the recording screen owns the
timer and twenty bounded amplitude-history bars. After the 180 ms preview is
rendered, the input owner joins background work and prepares stack/codec/buffers/
file outside the LVGL lock. The 72% arc repeats a 900 ms rotation on the LVGL
task throughout this work; no additional preparation task/stack is introduced.
The one-second timer is a minimum hold threshold; only a ready capture from the
same release epoch can open the sampling gate. Prepared resources cancel on
release, and the input loop can requeue a dropped completion after readiness.
Logs split background-join time from audio/file preparation time.
The existing 200 ms heartbeat
shifts actual input levels left; no additional task/timer or simulated signal
is introduced. The newest three samples are brighter. Confirmation hides live
input/history. History uses twenty static bytes; additional widgets use the
existing LVGL heap and still need board-level heap/display acceptance.

Pure tests cover the threshold, tail lifetime and history bounds. Actual input-
loop tests cover cancel/release/click, a fresh click, a delayed queued tap,
lock denial, stale timer completion, failed starts, recording-release click tails,
waveform movement and confirmation visibility. Deep/backlight wake tests remain
in the complete gate. Geometry and sanitizers do not prove tactile timing or
physical glyph rendering.


### Chinese capture and detail captions

Preparation/record/save titles, capture and input captions, and transient
save/recovery errors use the existing 16 px CJK label font (31 px line height).
The capture caption is at y=48, timer y=88, and live input caption y=224, with
numeric percentage still using the terminal font at y=230.

Detail transfer/text/error statuses and the volume caption also use that CJK
font. The frameless text panel is 144 px high at y=80, beneath title/metadata,
leaving the complete 31 px status line at y=236 clear of the playback bar at y=274. Numeric transport/timing and
physical OK labels retain their existing fonts and semantics. No font assets
or button behavior changed.

Coverage checks verify all static literals against the actual generated cmaps.
Width checks use actual glyph advances, pair kerning and LVGL rounding for all
status strings at 0/20/80/100% volume. Actual UI/app tests check CJK font selection
and separation from the timer, waveform, input percentage and playback bar.
Physical glyph rendering remains a device check.


### Unified terminal views and host rendering

The application now shares one Chinese header, right-aligned cached battery and
segmented rule. Palette and page geometry are authoritative in the
[visual language](../design/visual-language.md). Menu rows are borderless, detail
separates metadata/reading/status/transport, settings editing hides background
rows, and rejection decisions hide list content and status indicators. Existing input,
storage, worker and server protocols are unchanged.

Real LVGL rendering exposed retained bottom alignment on the capture feedback
label: subsequent position offsets could move it off-screen. Its constructor
now uses absolute coordinates. The object fixture models persistent alignment
and regression checks keep capture/confirmation feedback in bounds. Battery
registration covers all six persistent pages rather than silently omitting the
sixth. No additional firmware buffer, task or font resource was introduced.

Local RGB565 captures use the pinned LVGL and actual UI/font sources with
synthetic data. They cover list, detail, preparation, capture, confirmation,
settings, focused edit, provisioning and rejection. Real-engine bounds and
visual review supplement the complete gate; the host-only full framebuffer is
not linked into firmware. These previews establish neither board timing nor
physical brightness, clipping or memory headroom.


### Adjacent details, meters and post-blank sleep

Direction holds map to separate previous/next actions in detail, playing and
paused states; clicks retain volume control. `memo_detail_step` handles stable
within-page indices, remote page edges and a local prefix without repeating it
between pages. The input owner joins the old player and releases local playback
ownership before opening the target. Page requests remain asynchronous; a pending
navigation generation selects the correct remote edge after success. Failure or
empty results retain the original detail; OK hold cancels the pending UI switch.
Confirmation gestures begun during loading retain click-tail suppression even
when the page completes before release. UI rendering never performs network I/O.

`memo_progress.h` defines bounded fractions and content-coordinate insets for
1 px track borders. Both track types have equal 2 px outer gaps at full fill.
Playback uses a 56×8 track / 52×4 fill, while volume uses 160×12 / 156×8. Volume
animates from its current displayed width for 160 ms only when the target changes;
closing the editor stops that animation. Volume clamps at 0/100 and maps UP to
increase, DOWN to decrease; other timing option ordering is unchanged.

`memo_power.blank_since_ms` starts on the first actual blank decision, independent
of activity and of the sleep interval. Sleep requires both a blank display and
the complete configured delay. Always-on and keep-on states reset this timer.
Pure tests cover shorter sleep than screen timeout, delayed polling, wake/reset
and disabled timers; existing file/worker shutdown guards remain unchanged.
Actual input-loop tests cover adjacent and cross-page detail switching, playback
join faults, local file ownership and loading gesture tails. Real LVGL checks
verify symmetric meter gaps and a volume-animation midpoint/end across unchanged
refreshes, supplementing the object tests and sanitizers.
