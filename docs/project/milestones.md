<p align="right">
  <a href="milestones.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Voice Memo — Project Milestones

Updated 2026-11-09 against the current `feature/voice-memo` code. This is the
single place for implementation follow-up, delivery status and acceptance.
The [product](../product/product-spec.md), [client](../technical/client.md),
[server](../technical/server.md) and [visual](../design/visual-language.md)
documents describe current behaviour rather than a backlog.

## 1. Milestone overview

| Milestone | Current implementation | Delivery state |
| --- | --- | --- |
| M0 — Scaffold | Go service, application entry, manifests, fonts and custom partitions exist | Implemented; validation is reported per run |
| M1 — Server | Upload, original-container storage, metadata CRUD, ASR jobs, mDNS and SSE exist | Backend functionality implemented; current race/E2E validation below |
| M2 — Capture | One-second arming, held capture, release/cap confirmation, Opus spool and local playback exist | Draft/recovery/upload ownership implemented; concentrated device acceptance pending |
| M3 — Browse and settings | Paginated five-row viewports, bounded long text, pause/resume, setting drafts and reproducible CJK fonts | Code implemented and host-verified; physical visual/interaction acceptance pending |
| M4 — Network | Wi-Fi/SNTP, checked BLUFI handoff, mDNS, upload, streaming and SSE | Code implemented and fault-tested; radio/device acceptance pending |
| M5 — Acceptance | Idle/deep wake control, lifecycle diagnostics and application/worker/server regression | Software gates and code audit complete; board/provider acceptance pending |

A state-table transition is not an implemented application action. Historical
logs do not establish that every path in the current checkout works.


## 2. Historical device evidence

The previous M4 close-out notes recorded these observations. They are retained
as historical evidence, not new device tests performed during this documentation
update; no exact firmware hash was recorded alongside them.

| Area | Recorded evidence |
| --- | --- |
| Record → spool → reboot | 170 frames / 10.2 s survived a power cycle |
| Local playback | Spool container decoded and was audible |
| Bluetooth provisioning | BLUFI handshake, WPA2 association and IP acquisition |
| mDNS | PTR/TXT `ip=` fallback reached the service on port 8080 |
| Upload/transcription | HTTP 201; server record became `ready` with text |
| List/detail | Server data and transcript displayed |
| SSE | uploaded → transcribing → ready observed within two seconds |
| Server limits | `max_memo_ms=300000` read and checked before upload |
| Connected recording | Recording worked while Wi-Fi remained associated |
| Arming ring | Filled over one second |
| Remote playback | Frames decoded directly from `/audio`, without local cache |

Recorded free heap: about 231 KB after boot, 100 KB after Wi-Fi, 84 KB with
upload/SSE, largest contiguous block 73 KB. Encoder allocation was about
24.6 KB. These are one device/session's observations, not worst-case bounds.

Upload rejection to `.bad` and loss of network during recording were explicitly
not exercised in those notes.

## 3. Follow-up register

Unless marked implemented, entries below remain open; implementation does not imply device acceptance. Priorities and acceptance describe follow-up work;
firmware continuation is now authorised, with device testing explicitly
postponed as recorded in section 7. Git publication remains unauthorised. P0 protects recording data and service ownership; P1 restores usable
navigation/contracts; P2 covers polish and operational completeness.

### M2 — Capture and persistence

| ID | Priority | Evidence in current code | Work and acceptance |
| --- | --- | --- | --- |
| M2-01 | P0 | Implemented: `.rec → .draft → .memo`; runtime and persistent upload guards; draft confirmation restored after reboot | Host checks PASS; concentrated device acceptance: capture/upload overlap, reboot before save, explicit save/cancel and no remote entry after cancel |
| M2-02 | P0 | Implemented: batch/error recovery and acknowledged stop, non-destructive mount, preserved NVS, priority-bounded spool snapshots and fail-closed directory reads | Real-file and worker tests PASS, including 20 old acknowledged files, interrupted capture visibility and immediate scan-error guards; full SPIFFS/power/Flash corruption remains device acceptance |
| M2-03 | P1 | Corrected implementation: three rejected attempts per round, retry/delete, same-ID retry, single-slot blocking through every unresolved state; old `.keep` is compatibility input only | Host tests pass for a fresh three-attempt round and legacy drain; new firmware device acceptance remains: retry again, successful upload/deletion releasing the slot, offline/reboot recovery and no new local archive |
| M2-04 | P1 | Implemented: monotonic wall-clock display/warning/cap plus recorder-side 5000-frame bound, audio never exceeds 300000 ms | Host wall-clock and delayed-heartbeat frame-cap checks PASS; measure 4:30/5:00 timing, release-to-confirm and sample duration on hardware |

### M3 — Browsing, settings and visual fit

| ID | Priority | Evidence in current code | Work and acceptance |
| --- | --- | --- | --- |
| M3-01 | P1 | Implemented: moving five-row viewports, selected-ID preservation and bidirectional page actions; list errors isolated from detail navigation | Pure/UI/application action tests PASS, including failed/cancelled pages and detail edge opening; physical round-safe rendering remains |
| M3-02 | P1 | Implemented: collapsed entry, async 2 KiB text, line scrolling, adjacent windows, version restart/cancel, autoplay and play/pause/resume | API, real detail and actual application journeys PASS; real long-text/audio navigation remains device acceptance |
| M3-03 | P1 | Implemented: file/HTTP pause retains worker, decoder, source and progress; stop timeout retains ownership | Actual-worker host tests cover resume without replay, fragmented HTTP, stale acknowledgements and blocked restart; confirm audible boundaries and prolonged remote pause on device |
| M3-04 | P1 | Implemented: side-effect-free drafts, save/cancel, NVS error/retry and post-commit volume; invalid legacy integers validated before narrowing | Real settings host tests cover cancel/no writes, failed writes/commits/rollback, retry, reboot and unrelated-field preservation; actual UI hints and volume persistence remain for device acceptance |
| M3-05 | P2 | Implemented: truthful states, bounded UTF-8 labels, glyph policy, battery, three-second notices, UUID deduplication and separate SSID marquee | Object/glyph/notice and actual app tests PASS, including render timeout and accepted-reader overlap; physical state/corner checks remain |
| M3-06 | P2 | Implemented: pinned source/converter/payload manifest, OFL/notice, inventory, reproducible generator, all static UI glyph checks and dynamic unsupported-character policy | Static coverage/metric/provenance and cached/fresh-source reproduction PASS; widget/font rendering remains device-only |

### M4 — Network and worker lifecycle

| ID | Priority | Evidence in current code | Work and acceptance |
| --- | --- | --- | --- |
| M4-01 | P0 | Implemented: enter after all network ownership releases, failed entry rollback, host/profile acknowledgement, checked cleanup and success/failure/cancel restoration | Actual BLUFI, app and pure coordinator fault tests PASS; named mini-program and repeated physical Wi-Fi/BLE handoff remain device acceptance |
| M4-02 | P0 | Implemented: stop/deinit/handler failure retains ownership, checked STA startup, old-session/config rejection, copied caches and strings, serial mDNS and full low-memory restoration | Actual STA/BLUFI/worker/app lifecycle faults PASS; physical slow HTTP/BLE/low-memory behaviour remains device acceptance |
| M4-03 | P1 | Implemented: input loop runs the 200 ms heartbeat across pages; arming callback only enqueues; UI waits until startup completes | Device acceptance remains for live list/playback/provisioning refresh through every navigation and save/cancel path |
| M4-04 | P1 | Implemented: API-v1 verification, generation-safe invalidation, limits and queued manual discovery retained offline/busy | API/generation/limit/uploader and actual settings action checks PASS; moved/unreachable service needs device acceptance |
| M4-05 | P1 | Offline capture and AP-loss-during-capture branches covered by actual app tests with injected worker/network boundaries | Real disconnected recording, save, reboot/reconnect, intact audio and one accepted record remain hardware acceptance |

### M1 — Server contract and operations

| ID | Priority | Evidence in current code | Work and acceptance |
| --- | --- | --- | --- |
| M1-01 | P1 | Implemented: nonempty exact packet count, declared/frame duration agreement and caps, unique partials, non-replacing audio publication and atomic memo/job insert | Go race tests PASS for malformed counts/duration, actual frame cap, concurrent same-ID replay and enqueue rollback. Upload remains structural; Opus decoding errors are handled during transcription |
| M1-02 | P1 | Implemented: trimmed device ID restricted to `[A-Za-z0-9_-]{1,64}` before filesystem use; cross-device UUID replay returns 409 | Traversal, malformed IDs, boundary length and legitimate MAC/named device cases PASS in Go tests |
| M1-03 | P2 | Implemented: actual 1–4-worker pool, 90s attempt context, job/attempt ownership guards, joined shutdown; dry-run/explicit GC with 24h floor; nonzero inactive retention option rejected | Go race/E2E checks cover pool count/cancellation, stale results, preview/apply, reference/rejected/recent/symlink preservation. No automatic recording expiry |
| M1-04 | P1 | Implemented: E2E compares original MEMO bytes and exact Range bytes/headers; canonical WAV claim removed from handler | Real-process fake-ASR E2E PASS; Go adapter/decode tests and race gate PASS |
| M1-05 | P2 | Implemented: central 40-rune snippets, management notifications, slow-subscriber disconnect, bounded/resettable firmware SSE framing and coalesced authoritative refresh on event/reconnect | Go mutation/payload tests and host fragmentation/overflow/reset/cache tests PASS; long sessions and dropped-network recovery remain in concentrated device acceptance |

### M5 — Power, regression and final acceptance

| ID | Priority | Evidence in current code | Work and acceptance |
| --- | --- | --- | --- |
| M5-01 | P1 | Implemented: idle blank/sleep guards, complete wake-gesture consumption, atomically published boot guard and ordered terminal shutdown | Policy/fault-order plus actual held/released wake-boot tests PASS; real peripheral shutdown, current and wake-to-list remain device acceptance |
| M5-02 | P1 | Implemented: actual boot/input/actions/UI and BLUFI harnesses, every normal action, heartbeat, fault and ownership assertions, worker and server race/E2E suites | Native/Linux, sanitizer, server and font evidence recorded below; consolidated hardware/provider regression remains |
| M5-03 | P2 | Encoder now allocates per capture and releases only after acknowledged stop; earlier heap observations remain one device session | Measure peak heap/largest block/stack with the new lifetime and repeat capture, streaming, reconnect and provisioning; inspect display/audio timing |

## 4. Validation and delivery rules

For implementation, run focused checks, then the complete gate with ESP-IDF
5.5.3 activated. Server tests are separate. The host gate selects the native
macOS/Linux linker flag; the Linux wrapper remains an optional CI check.

```bash
source scripts/idf-env.sh
./tools/validate.sh --static
./tools/validate.sh --firmware
./tools/validate.sh
./scripts/static-gate-linux.sh
```

```bash
source scripts/go-env.sh
(cd server && go test ./...)
./scripts/e2e-server.sh
```

Record each run's actual outcome; a prior artifact does not validate a failed
run. Device acceptance must include the capture/read/play loop, provisioning
re-entry, lost-network recovery, capacity/error cases, long text and power/wake
behaviour. Flash only the exact validated firmware after explicit approval;
commits/pushes are also separately authorised.

Report `Build`, `Host tests`, `Device tests` and `Unverified` separately. A
firmware build, successful upload or old log is not full hardware acceptance.

## 5. Earlier documentation synchronisation

Scope: English/Chinese product, client, server, visual and milestone documents,
plus README/index links and the baseline/custom partition-layout distinction. Firmware, tests and scripts are unchanged. Source
review is not a new device test.

| Check on 2026-10-27 | Result |
| --- | --- |
| `python3 tools/check_repo.py`, `git diff --check` | PASS |
| Native `./tools/validate.sh` with ESP-IDF 5.5.3 | FAIL at macOS `ld: unknown options: --gc-sections`; firmware stage not reached |
| `./scripts/static-gate-linux.sh` | PASS, complete static gate and host tests |
| `./tools/validate.sh --firmware` | PASS after retry outside the sandbox; initial CMake process inspection was blocked by sandbox `sysctl` permissions |
| `python3 tools/archive_firmware.py verify <archive>` | PASS |
| `(cd server && go test ./...)` with `scripts/go-env.sh` | PASS (cached results) |
| `./scripts/e2e-server.sh` | NOT RUN; stale RIFF assertion tracked in M1-04 |
| Device tests | NOT RUN; documentation-only change, no flash |

Build: PASS. Host tests: PASS (Linux gate and Go tests). Device tests: NOT RUN.
Unverified: hardware acceptance and the open items in section 3, including the
real-process E2E contract correction. The native macOS complete command does not
pass as-is; static and firmware gates passed separately on the platforms above.
Existing compiler deprecation/unused-symbol warnings remain under M5-02.

Validated firmware identity: ESP-IDF `v5.5.3`, app version `55734fb-dirty`.
Application size 3373440 bytes in a 7274496-byte factory partition;
merged image 3438976 bytes. Local archive:
`build/firmware/3e6a84c6f05ffa76f04ab608c6f3ea73527b5711ac1b77540c4ab423c946d480/`.
Full-image SHA-256:
`3e6a84c6f05ffa76f04ab608c6f3ea73527b5711ac1b77540c4ab423c946d480`.
Matching ELF SHA-256:
`5186eb5cd38e7bf0a5d2725f6887d896b61ab4d3d3dfc1f650d9694956f2dbe3`.
These artifacts are ignored local outputs, not a published release or device result.



## 6. Rejected recording retry/delete — 2026-10-27

Accepted behaviour: **three rejected attempts per round including the first**,
then retry/delete on the list. Retry is the default and OK hold also retries.
The same memo ID returns to pending with a fresh three-attempt budget. Until
upload success or explicit deletion, the processing slot remains occupied and
new capture is blocked, including during retry/offline/reboot. No new `.keep`
archive is created. Existing `.keep` files are unresolved compatibility inputs;
any already-existing backlog is preserved and drained one item at a time.

The choice shows recording time/duration and waits for worker acknowledgement.
Failed retry/delete leaves the file and dialog intact. Pending retry takes
priority over another legacy review; a third rejection publishes that same memo
for review. The list tick resumes when the choice closes so eventual upload
success can refresh the list. State, retry policy, file operations, layout and
fixed-string glyph coverage have host checks.

### Device exercise of the superseded build

The user approved flashing image
`ba530b18535f24752cd2d58a8739fbc1789c3fc350b272fcf1e900c6a715e1c7`
on `/dev/cu.usbmodem14301`. ESP32-C3/8 MB Flash and an identical partition table
were confirmed. Matching bootloader/table/app segments were written and verified;
NVS/PHY and the spool were outside the erase/write ranges. Boot reported the
matching ELF prefix `6e40b085b`, ESP-IDF 5.5.3, spool mount and Wi-Fi reuse, with
no panic/watchdog/missing-glyph error in the captured windows.

The user confirmed clear Chinese dialog/time/duration and successful local
playback after choosing keep. A temporary LAN service, storing no audio,
received exactly three HTTP 400 uploads of the 11178-byte / 4620 ms test memo;
intervals were 5.454 s and 10.509 s. An earlier 720 ms capture was cancelled and
was not uploaded. The service stopped with three total POSTs and serial
monitors were closed.

This exposed the product conflict: keep released the capture guard and allowed
another local memo. That behaviour is superseded by retry/delete. The user did
not confirm deletion; no on-device explicit-delete result is claimed. These
observations apply to the old image, not device acceptance of the corrected
firmware. The device remains on the old image until the corrected image is
approved for another flash.

### Corrected implementation checks and acceptance

Host tests cover repeated three-attempt rounds for the same ID, capture blocked
before/during/after retry, release only on successful upload or explicit delete,
failed retry/delete/terminal rename, offline review, legacy `.keep` retry/delete,
stale-ID rejection and sequential legacy drain. Real-file tests preserve header
and packet bytes through `.bad → .memo` and legacy `.keep → .memo`. Font and
layout tests cover retry/delete labels with the generated 31/38 px line heights.

M2-03 device acceptance still required for the corrected image:

1. Retry a rejected test memo: same ID, no new capture allowed, at most three
   rejected attempts before the choice returns.
2. Retry again: verify a fresh round and an occupied slot while offline or waiting.
3. Accept the upload on a service that durably stores the original container:
   only then may the local file disappear and the slot become free.
4. Explicitly delete only a disposable test memo; ensure no removal before OK
   and verify errors leave it unresolved.
5. Restart with unresolved `.bad`/legacy `.keep`: files remain, retry/delete
   returns, and recording stays blocked until every old item is resolved.
6. Confirm Chinese text, time/duration, default/hold retry, selection, live list
   updates and local playback on the corrected firmware.

Corrected-image validation: repository/bilingual/diff checks PASS; full Linux
static gate and host tests PASS; ESP-IDF 5.5.3 firmware gate and matching debug
archive verification PASS. The native complete command still stops at the
existing macOS GNU-linker flag incompatibility; the gates passed separately.

Build: PASS. Host tests: PASS. Device tests: NOT RUN (corrected image).
Unverified: the six corrected-image device checks above, especially renewed
retry, single-slot occupancy and legacy `.keep` handling. The server is unchanged;
the temporary rejection service and serial monitors are stopped. No commit/push.

The corrected image has not been flashed. Full-image SHA-256:
`700c3ae01916976e052cea09169e21dfaa6c825195601290e558ac5ceea391e2`.
Matching ELF SHA-256: `7013918c3b8358e220f1afa436f1e841e6db254e35cc47e7003951012a575025`.
Local archive: `build/firmware/700c3ae01916976e052cea09169e21dfaa6c825195601290e558ac5ceea391e2/`.
Application 3377152 bytes, merged image 3442688 bytes, version `55734fb-dirty`.
The merged image contains the retry prompt and no longer contains the old keep
prompt. Earlier-image approval does not cover this new flash; after confirmation,
compatible segmented flashing can again protect NVS/PHY/spool data.

## 7. Software continuation; consolidated device testing deferred — 2026-10-27

The user authorised continuing the remaining milestones and explicitly postponed
flashing/device acceptance until functionality is more complete. This phase does
not request or perform another flash. The board remains on the superseded image
recorded in section 6, and existing recordings must be preserved.

This increment implements M2-01, the software recovery/error portion of M2-02,
the wall-clock part of M2-04, M4-03, the stop/rejoin portion of M4-02, and server
items M1-01/M1-02/M1-04. Recovery preserves complete stored packets as a draft,
requires explicit save before upload and blocks processing after recovery I/O
failure. A partial trailing packet and unflushed RAM cannot be recovered.
The existing mount auto-format policy is still an open M2-02 data-risk item.

Fault coverage executes the real recorder, uploader and SSE workers with injected
boundaries, plus the real spool against temporary files. It includes read/encode/
write failure, flushing earlier valid frames after audio failure, stop timeout,
late acknowledgement, prevention of a second worker, wall time, interrupted
headers/tails, idempotent recovery, draft commit/cancel, state-name collision
preservation and a recovery-failure upload hold. SSE tests cover idle read timeout
and HTTP cleanup, not complete application provisioning or event reconciliation.
Server race tests cover path IDs, duration/count invariants, simultaneous replay
and transaction rollback; E2E verifies original audio and exact Range bytes.

Remaining software work stays in the register: mount failure policy, provisioning
handoff/cache/discovery ownership, list/history/detail navigation, real pause,
settings save/cancel, status/font completion, bounded event reconciliation,
server operational options and automatic power management. Device-only checks
are collected for the later session: full/failed Flash and power interruption,
three-attempt retry/delete and legacy data, offline/reconnect/provisioning,
long text/navigation/settings, recording limit, audio quality, memory and power.

Validation for the final code is recorded below. The earlier image in
section 6 and intermediate local builds do not validate this increment.

| Final-code check | Result |
| --- | --- |
| Repository/bilingual checks and `git diff --check` | PASS |
| `./scripts/static-gate-linux.sh` | PASS, including real recorder/spool/upload/SSE host tests |
| Native `./tools/validate.sh` under ESP-IDF 5.5.3 | FAIL at existing macOS `ld: unknown options: --gc-sections`; firmware stage not reached |
| `./tools/validate.sh --firmware` and archive verification | PASS |
| `(cd server && go test -race -count=1 ./...)` | PASS, non-cached; rerun with loopback permissions after sandbox denied the ASR test listener |
| `./scripts/e2e-server.sh` | PASS with loopback permissions; original MEMO, exact Range, upload/replay/transcription/CRUD |
| Device tests | NOT RUN, deferred by user; no flash or device data change |

Build: PASS. Host tests: PASS. Device tests: NOT RUN.
Unverified: the consolidated device checklist above and software items still
open in the register. The native complete command still fails as stated; Linux
static and native firmware gates passed separately. Existing LVGL deprecation
and unused-helper warnings remain. No commit/push or publication was performed.

Validated ESP-IDF `v5.5.3`, app version `55734fb-dirty`; application 3380512 bytes
in a 7274496-byte factory partition, merged image 3446048 bytes. Verified local
archive: `build/firmware/fc80b65cd6db94dffe0264c1820800380cffb102d7bc66c0483fca59ea48b33f/`.
Full-image SHA-256: `fc80b65cd6db94dffe0264c1820800380cffb102d7bc66c0483fca59ea48b33f`.
Matching ELF SHA-256: `c1fcadb03c4a86c16b27472e37d4114bb680e9c1fd1adeec368d1f43aa6134b6`.
These ignored local artifacts have not been flashed or published.

## 8. Backend functionality first; UI and hardware deferred — 2026-10-27

The user explicitly moved UI work and device acceptance to the final phase.
This increment therefore develops storage, network ownership, protocol and
server operations. Existing provisioning actions are connected to the backend
coordinator; screen layouts/styles, fonts and control hints are unchanged.
No device was opened/flashed and no new commit/push was performed.

Implemented in this increment:

- M2-02: mount without automatic erasure of existing data; initialise only after
  reading and verifying the entire partition erased. NVS init errors also keep
  credentials/settings intact.
- M2-04: recorder-side 5000-frame limit prevents a delayed heartbeat from
  exceeding the server five-minute audio cap.
- M4-01/M4-02: testable ownership coordinator for stop/entry/rollback/cancel/
  success/failure/restart. Failed host shutdown retains resources. Metadata has
  one writer and copied mutex reads; mDNS query/teardown and address snapshots
  have separate locks.
- M4-04: API-v1 health and valid limits before POST, generation-safe invalidation
  and automatic rediscovery on network/5xx failure, both byte and duration caps.
- M1-03: bounded 1–4-worker pool, 90 s attempts, cancellation during container
  reads/HTTP, pool join on exit. Exact job-ID/attempt ownership gates result and
  queue transactions so edit/delete/requeue/expired-lease results cannot overwrite
  newer work. Replay of an explicitly deleted UUID returns terminal 410 instead
  of recreating it or causing endless 5xx retries. SQLite uses FULL synchronous commits; accepted audio synchronises
  the directory chain as well as file contents.
- M1-03: `gc` previews by default; explicit `--apply` removes only unreferenced
  MEMO/partial files older than the minimum 24 h. Referenced/tombstoned audio,
  rejected files, recent files and symlinks are preserved. Nonzero unsupported
  retention settings now fail startup instead of silently doing nothing.
- M1-05: central bounded snippets and management notifications, slow-subscriber
  disconnect and bounded/resettable firmware framing. Events/reconnects request
  coalesced authoritative list reads, including empty edits and deletions.

After this increment, pause and power were implemented in section 9. The
remaining UI phase includes list/history/detail navigation, settings interaction,
server-row manual action, Wi-Fi/provisioning display snapshots and
status/font/battery fit. Physical
acceptance remains deferred: real Flash/mount interruption, repeated BLE/Wi-Fi
handoff and slow requests, moved services, audio/long text, memory/stack and power.
Host coordination tests do not exercise the NimBLE platform implementation;
`nimble_port_stop()` itself may block before the acknowledgement wait.

| Final-code validation | Result |
| --- | --- |
| Repository/bilingual checks and `git diff --check` | PASS |
| Linux static gate | PASS, including spool mount/recovery, uploader limits, actual API cache/body limits, endpoint generations (including partial lock-allocation failure), SSE framing and handoff fault tests |
| Go `go test -race -count=1 ./...` | PASS; loopback permission used for httptest |
| Real-process fake-ASR E2E | PASS, including live-service GC preview/apply, preservation of accepted/rejected audio and exact original/Range bytes |
| Native complete gate | FAIL at the existing macOS `--gc-sections` linker incompatibility; firmware stage not reached |
| ESP-IDF 5.5.3 firmware gate and debug archive verification | PASS; an initial GCC truncation warning was fixed using existing UTF-8 safe copying before this successful build |
| Device tests | NOT RUN, explicitly deferred; no flash |

Build: PASS. Host tests: PASS. Device tests: NOT RUN.
Unverified: the deferred UI/integration and physical checks above. Linux static
and native firmware gates passed separately; the native complete command remains
blocked as recorded. Existing LVGL deprecation/unused-helper warnings remain.

Verified firmware: version `5ddf0ee-dirty`, application 3389520 bytes in a
7274496-byte factory partition; merged image 3455056 bytes. Local archive:
`build/firmware/f26698f6d6cfbf5c30b458c293a9f3a072fe0ce8fb6d76f6e30cacfe97ba43da/`.
Full-image SHA-256: `f26698f6d6cfbf5c30b458c293a9f3a072fe0ce8fb6d76f6e30cacfe97ba43da`.
Matching ELF SHA-256: `a628ba3da7d36905db76a568f0b87ee1dc2ae4e807c43212247e642036ce34a1`.
The artifacts are ignored local outputs, not a flash, commit or publication.

## 9. Idle power and true pause/resume — 2026-10-28

M5-01 now has an application power loop driven by the existing input heartbeat.
Physical events reset idle time; the first complete gesture after backlight
blanking is consumed, including a click delivered after release. Recording,
arming, save/rejection decisions, active/paused audio, provisioning and held or
unreadable keys keep the screen on. Unresolved spool data and worker recovery
block deep sleep. Zero timeout/sleep values disable their respective timers.

The pure shutdown coordinator requests both network workers and playback to
stop, requires acknowledgements, rechecks data/input safety, locks LVGL and arms
GPIO wake before terminal teardown. Failed preflight recovers workers and resets
the idle interval. Terminal shutdown releases discovery/Wi-Fi/buttons, then
battery, codec, I2S, shared I2C and display in that order. Button failure or an
unexpected sleep return restarts; other peripheral failures are logged while
shutdown continues. Boot starts at the list and suppresses the wake gesture.

M3-03 now retains the playback worker, source, decoder and progress while paused,
for both local files and remote HTTP. Resume continues the same source. A current
I2S write may finish after pause; a dropped remote connection ends playback and
does not restart it. Stop timeout retains ownership; late completion is joined
before another worker or recording can use the codec. HTTP reads check stop
between fragments and use a two-second socket timeout.

The new power host suite covers idle boundaries, disabled timers, active/data
protection, complete wake gestures and injected shutdown faults, including a key
arriving after worker stop. The actual-player suite covers local and fragmented
HTTP pause/resume without frame replay, stop timeout, natural/late completion,
stale acknowledgements and failed task creation. These are software tests, not
measurements of real peripheral suspend or audible pause boundaries.

| Final-code validation | Result |
| --- | --- |
| Linux complete static/host gate | PASS, including the two new suites |
| ESP-IDF 5.5.3 firmware gate and archive verification | PASS |
| Native complete gate | FAIL at existing macOS `--gc-sections`; firmware stage not reached |
| Go race/E2E | Not rerun; server unchanged since section 8 |
| Device tests | NOT RUN, explicitly deferred; no device access or flash |

Build: PASS. Host tests: PASS. Device tests: NOT RUN.
Unverified: real idle timing/current, terminal peripheral shutdown, each-key wake,
wake-to-list and audible/long remote pause; existing UI and concentrated hardware
items in the register. UI layouts, font assets and visual styling were unchanged.
Next functional work is settings save/cancel/persistence errors, followed by the
remaining browsing/UI integration. No commit/push/publication was performed.

Verified local archive:
`build/firmware/ecb7532482fe026c7a2e848ddebe5848a99905e33644f88d4e130073cd67e838/`.
Full-image SHA-256: `ecb7532482fe026c7a2e848ddebe5848a99905e33644f88d4e130073cd67e838`.
Matching ELF SHA-256: `f6303fe5de0539b8c8df0088d003f120155e3cc53cf454d632a22c2e661c84c7`.
ESP-IDF `v5.5.3`, app `5ddf0ee-dirty`; application 3403536 bytes in a
7274496-byte factory partition, merged image 3469072 bytes. Ignored local artifacts,
not flashed or published.

## 10. Settings drafts and native validation — 2026-10-28

M3-04 now edits volume/timeout/sleep in a side-effect-free draft. Adjusting and
cancelling do not write NVS or alter audio. Save commits only the selected field,
then publishes runtime settings and applies volume. A write/commit error leaves
the draft and overlay open for retry/cancel; autoplay failure also preserves its
confirmed value. Save/toggle work runs outside the LVGL lock. Editing keeps the
screen on. No preview tone is claimed or offered.

NVS init/open errors preserve data and return errors. Legacy integer options are
validated before narrowing, preventing wrapped values from changing power timing.
A failed write/commit attempts to restore the previous confirmed key; persistent
Flash errors still cannot guarantee rollback. No error path erases NVS.

The settings host suite executes real code with immediate-write NVS fault stubs:
cancel/no writes, failed write/commit/rollback, draft retry, restart reload,
unrelated-field preservation and invalid signed/oversized legacy values. Font
coverage includes the existing failure-hint text at both generated sizes.

The host gate now selects Apple's dead-strip flag on macOS and GNU section GC
on Linux. All existing runtime fixtures still run. The Linux wrapper remains an
optional reproduction of CI; a native full run can now reach the firmware gate.
The earlier failed native runs remain historical results in sections 5–9.

| Final-code validation | Result |
| --- | --- |
| macOS native static/host gate | PASS |
| Linux static/host gate | PASS |
| Native complete gate | PASS, including static checks, all host suites, ESP-IDF build and merged-image verification |
| Go race/E2E | Not rerun; server unchanged since section 8 |
| Device tests | NOT RUN, deferred; no flash |

Remaining code work is history pagination, long-detail navigation, UI fit/status/
fonts and network display snapshots, manual server rediscovery and action/render
reconciliation after an LVGL lock timeout. Those are tracked in M3/M4/M5 above;
physical power/audio/Flash/BLE and real SiliconFlow acceptance remain deferred.
No commit, push or publication was performed.

Build: PASS. Host tests: PASS. Device tests: NOT RUN.
Unverified: device settings/error rendering, actual volume/reboot persistence,
remaining browsing/UI work and the consolidated physical/real-provider checks.
The earlier separate firmware command generated a verified image but exited on
script parsing after the script changed during execution; this final complete
run used the fixed script and exited successfully. Existing LVGL deprecation and
unused-helper warnings remain.

Verified final archive:
`build/firmware/2e9eeb0b971dd5edd1d221e136d895e9c897f6c7d55a47f2245e25dc81c82fa6/`.
Full-image SHA-256: `2e9eeb0b971dd5edd1d221e136d895e9c897f6c7d55a47f2245e25dc81c82fa6`.
Matching ELF SHA-256: `afdd7700c4e2220c41a8f4100ebd6fc005757aaee0e745316fd5da6ea7feb1c5`.
ESP-IDF `v5.5.3`, app `5ddf0ee-dirty`; application 3404624 bytes in a
7274496-byte factory partition, merged image 3470160 bytes. Ignored local artifacts,
not flashed or published.

## 11. History and bounded text backend — 2026-10-28

The backend part of M3-01/M3-02 is implemented. Server listing adds
`previous_cursor` and `direction=newer`, selecting nearest newer rows while
returning newest-first order. Rows/cursor existence share a transaction. Exact
final pages have no extra cursor; deleted pages retain an escape direction.
Forward-only callers keep the existing contract.

The additive `/memos/{id}/text` endpoint returns bounded UTF-8 display windows
with byte boundaries and SHA-256 revisions. It supports forward/backward reading,
empty/status-only text, NUL/invalid-UTF-8 replacement, deleted-record rejection
and 409 on edits during reading. The full-detail/audio contracts are unchanged.

Firmware API code retains one metadata page and one 2 KiB text window. UI-facing
requests queue work for the uploader; rows/cursors copy together, failed moves
preserve the displayed page and serials discard late responses. Text cancellation
or a new selection invalidates old requests. Transport/5xx failures retry; schema,
version and missing-record errors return to the reader. Status storage now fits
`transcribing`; UTF-8 validation rejects overlong/surrogate/out-of-range encodings.

These queued APIs are not yet connected to page controls: the current executor
still draws five fixed cards from the latest page and calls the old 512-byte
transcript helper. The next UI phase must connect page boundaries, selected-row
visibility, text-window scrolling/cancellation/version restart and coherent
rendering. Settings fit, status/placeholders/battery, font provenance/coverage,
network display snapshots/manual discovery and LVGL-timeout render recovery
also remain in the register. No claim of complete browsing is made by API tests.

| Final-code validation | Result |
| --- | --- |
| Native `./tools/validate.sh` under ESP-IDF 5.5.3 | PASS, complete static/host/build/merged-image gate |
| Archive verification, repository/bilingual checks, `git diff --check` | PASS |
| `go test -race -count=1 ./...` | PASS, including 72-record forward/backward ties and deleted-page recovery |
| Real-process fake-ASR E2E | PASS: 51 records across pages and backward return; exact forward/backward reconstruction of 36000-byte mixed Unicode text and stale revision rejection; existing audio/GC/CRUD checks retained |
| Device tests | NOT RUN, deferred; no device opened or flashed |

The server text unit test reconstructs 144000 bytes in both directions; firmware
host cases inject bad bounds/revisions/UTF-8, failed and superseded requests and
cancellation. E2E first failed its five-second startup wait; retry passed, then
startup diagnostics and a 15-second bounded readiness wait were added and the
final script passed. No root cause was established from the first discarded log.

Build: PASS. Host tests: PASS. Device tests: NOT RUN.
Unverified: the UI integrations above, physical RAM/stack/timing and rendering,
consolidated device checks and real SiliconFlow credentials/provider behaviour.
No new Chinese UI strings/fonts/layout changes, commit, push or publication.

Verified final archive:
`build/firmware/036ccb4afd9d7d256c1f830c324460b126c781c11ab3c6960f2b48fb7f81e1c8/`.
Full-image SHA-256: `036ccb4afd9d7d256c1f830c324460b126c781c11ab3c6960f2b48fb7f81e1c8`.
Matching ELF SHA-256: `43ff6871e0567a2d1a8d7f92750ff96a28e4d8bf9241612331c64d4a60bbd9ed`.
ESP-IDF `v5.5.3`, app `5ddf0ee-dirty`; application 3407856 bytes in a
7274496-byte factory partition, merged image 3473392 bytes. Ignored local artifacts,
not flashed or published.

## 12. Browse UI, text windows and font provenance — 2026-10-28

M3-01/M3-02 are connected to page actions. Five list/settings row slots follow
the selected row, all seven settings are reachable, and list/detail boundaries
queue adjacent server pages. Background list refresh preserves the selected ID;
detail owns a separate entry snapshot. Text requests are asynchronous, cancelled
on back/reselection, and restarted after revision conflict. Collapsed entry uses
OK to expand; expanded/play/pause use line scrolling and adjacent text windows.

M3-05 now distinguishes empty/failed/waiting/local/remote messages, constrains
label widths, and displays cached battery on all five pages. SOC sampling and
list I/O run outside LVGL. An input-owner reconciler retries drawing current
state after lock timeout without repeating NVS/network/audio effects. Some
transient notices still need the final controller/state pass; raw Wi-Fi/BLUFI
string reads and the server-row manual discovery action remain M4 work.

Local playback now holds its source until player join, including pause/stop
timeout. A successful POST already in flight defers local deletion; after reader
release the same ID is posted idempotently and removed. The uploader host test
injects that reader acquisition during response handling and verifies no early
deletion. A detail follows the same ID remotely after the transient file is gone.

M3-06 has official Noto Sans SC 2.004 source hashes (both matched fresh downloads),
OFL/notice, the original requested inventory and converter 1.5.2/payload manifest.
`generate_memo_fonts.py --check` reproduced both C bitmap/metric/cmap payloads
from cached and freshly downloaded sources without changing maintained assets.
4012 emitted code points include all 3755 GB2312 level-one Han. Unassigned U+FF00
is deliberately absent. Static UI literals are checked against both sizes;
dynamic unsupported characters/invalid sequences become `?`, preserving other
supported text and server offsets.

The 16/20 px fonts actually have 31/38 px line heights. Host fixture metrics are
checked against the generated files; headers/dividers, confirmation choices,
settings overlay, footers and text scroll steps account for those heights.
Recording choices use CJK fonts, and provisioning hints now match UP/DOWN hold.
No pixel-rendered or hardware result is inferred from these object-model tests.

| Final-code validation | Result |
| --- | --- |
| Native ESP-IDF 5.5.3 `./tools/validate.sh` | PASS, complete static/host/firmware/merged-image gate |
| Linux/GCC static gate | PASS, including browser viewport, UI bindings, glyph/provenance and local-reader/upload overlap |
| Cached-source and fresh-cache font reproduction | PASS, both 16/20 px payloads match |
| Archive verification, repository/bilingual checks, `git diff --check` | PASS |
| Go race/E2E | Not rerun; server unchanged since section 11 |
| Device tests | NOT RUN, deferred; no device access or flash |

The first UI build found a GCC path-format bound warning; the UUID/extension
bounds were made explicit and subsequent final gates passed. Intermediate font
layout builds are superseded by the archive below. Existing LVGL deprecation
warnings remain. New host UI tests execute real UI modules against an object/
scroll model, not a raster renderer or the entire application task stack.

Build: PASS. Host tests: PASS. Device tests: NOT RUN.
Unverified: full application action/state/render integration, transient notices,
Wi-Fi/BLUFI display snapshots, manual server rediscovery, real memory/stack,
pixel/font/corner output, power/audio/BLE and real SiliconFlow acceptance.
Remaining code is tracked above; no commit, push, publication or flash occurred.

Verified final archive:
`build/firmware/e6db17bbc05d548cf794ddd7579448019bd8bc20baaf63c1c705e051079c4576/`.
Full-image SHA-256: `e6db17bbc05d548cf794ddd7579448019bd8bc20baaf63c1c705e051079c4576`.
Matching ELF SHA-256: `72f2eb110fde54e982ac615195b82b5c78cdddebbb3999307fc45b0b52ed5f6e`.
ESP-IDF `v5.5.3`, app `5ddf0ee-dirty`; application 3413360 bytes in a
7274496-byte factory partition, merged image 3478896 bytes. Ignored local artifacts,
not flashed or published.

## 13. Network display snapshots and manual rediscovery — 2026-10-28

M4-02 display consumers now copy bounded SSID/IP values under short critical
sections rather than retaining event-owned pointers. STA start clears a prior
SSID suffix, including no-credentials restart; 32-byte SSIDs are terminated in
the display copy. Disconnect/suspend clear the address and suspended STA ignores
late IP events. BLUFI configuration/report readers copy under a separate short
lock, with driver and BLE calls outside it. The IDF 5.5.3 BLUFI report API and its
BTC deep-copy implementation were inspected for the temporary SSID's lifetime.

M4-04's server row now queues manual rediscovery to the existing uploader writer.
Requests coalesce, wait offline/busy and interrupt network retry backoff; a new
request during a probe survives for the next cycle. Current endpoint invalidation
uses its generation-checked copy, then discovery/API-v1 verification runs before
refresh/upload. Failed discovery does not erase metadata. Settings displays
waiting-for-network or searching while the request is queued.

M3-05 capture-blocked/audio-unavailable feedback now lives in an app-owned
three-second notice rendered in the wider footer hint, surviving repeated
reconciliation. An accepted upload retained for a local reader is deduplicated
against the server page by UUID. Application tests for these two integrations
remain M5-02; focused notice tests are not claimed as a full button journey.

New tests exercise concurrent snapshot copies, shorter/full SSIDs, actual STA
event/suspend/restart handling, real uploader queuing/backoff and actual
settings/provisioning display bindings. Full `memo_app` input/action/render
reconciliation coverage remains the next code task, followed by the milestone
completion audit. Hardware, real SiliconFlow credentials, physical glyph/geometry,
RAM/stack, audio/BLE and power measurements remain deferred by the user.

| Final-code validation | Result |
| --- | --- |
| Native ESP-IDF 5.5.3 complete gate | PASS: static/host/build/merged verification |
| Linux/GCC static gate | PASS, including concurrent network snapshot and actual STA tests |
| Archive verification, repository/bilingual checks and `git diff --check` | PASS |
| Go race/E2E | Not rerun; server unchanged since section 11 |
| Device tests | NOT RUN, deferred; no device accessed or flashed |

Build: PASS. Host tests: PASS. Device tests: NOT RUN.
Unverified: full app action/render matrix, accepted-reader list overlap and notice
reconciliation; physical memory/timing/geometry/power/audio/BLE and real provider.
No commit, push or publication occurred. Existing LVGL deprecation warnings remain.

Verified archive:
`build/firmware/00a2ef4b7079d5daac2929ea354d0ec510d844bd6fab6f0e2bbc7ae430083cd8/`.
Full-image SHA-256: `00a2ef4b7079d5daac2929ea354d0ec510d844bd6fab6f0e2bbc7ae430083cd8`.
Matching ELF SHA-256: `f2fae62734c47549198bb9b335fbb845daf1f9ea80ecd991182b865c91434edc`.
ESP-IDF `v5.5.3`, app `5ddf0ee-dirty`; application 3414560 bytes in the
7274496-byte factory partition, merged image 3480096 bytes. Ignored local artifacts.

## 14. Actual application journeys and failure recovery — 2026-10-28

M5-02 now executes `app_main`, actual input-loop segments, action execution and
all real UI modules, with real state/settings/text/container code and injected
platform/worker boundaries. It observes every action reachable from normal boot;
legacy unused enum hooks and the unreachable ERROR entry are explicitly excluded
from that assertion and remain covered by pure state tests where applicable.
UI accesses assert the display lock. Side-effect counters verify that missed
render locks do not replay persistence, capture, playback or discovery requests.
Ten-receive continuous segments check the 200 ms heartbeat independently of the
short test steps. This does not simulate RTOS scheduling, concurrent real worker
execution, physical drivers or pixels.

Journeys include twelve-row movement and UUID preservation, older/newer/failure/
cancelled pages and detail page edges; 2 KiB text windows in both directions,
revision restart and cancel; play/pause/resume/autoplay and failed start; real
settings drafts/commit error/retry/cancel; arming/capture/cap/draft save/cancel,
recovered confirmation, offline/AP-loss branches, local-reader hold/UUID dedup;
rejection default retry, explicit delete, pending/failure and lock retry;
provisioning stop failure/rejoin, success/fatal exit and whole wake gesture.

Tests reproduced and fixes now cover:

- Missed startup display lock left list/record objects unbuilt. Reconciliation
  constructs missing persistent pages before rendering, without platform reinit.
- A failed list page polluted a healthy detail hint. Detail navigation now owns
  its error separately; failed detail movement preserves the ID and can retry.
- STA registration/read failures were ignored, repeated start reset a live IP,
  and a fast IP event could be overwritten at the end of startup. Registration
  retry only fills the missing handler, errors are published, connected start is
  idempotent and a late retry does not reconnect a live session.
- If capture borrowed network memory then stop failed, upload could remain
  stopped. The complete Wi-Fi/discovery/upload/SSE restoration now runs on both
  acknowledged outcomes, with failed STA/worker starts retried by heartbeat.
  Recorder stop timeout still retains capture ownership and defers restoration.

Full-length SSID uses a separate single marquee line above IP. Reapplying the
same glyph-filtered text preserves marquee progress, including unsupported input.

Final code audit remains open for M4-02: normal `memo_net_suspend` still ignores
Wi-Fi stop/deinit results. Propagate a failed driver release to provisioning and
low-memory capture handoff, retain its ownership until cleanup succeeds, and
verify retry before calling the code complete. Hardware, real provider and the
physical acceptance register remain deferred; no new device result is claimed.

| Final-code validation | Result |
| --- | --- |
| ESP-IDF 5.5.3 complete native gate | PASS: host/static/build/merged verification |
| Final native and Linux/GCC static runs | PASS; app fixture uses the real BSP pin header through host driver stubs |
| Actual application ASan/UBSan run | PASS; no sanitizer finding |
| `go test -race -count=1 ./...` | PASS |
| Real-process fake-ASR E2E | PASS: upload/idempotency/transcription, history/text, management, GC and audio Range |
| Archive verification, repository/bilingual checks and `git diff --check` | PASS |
| Device tests | NOT RUN, deferred |

An intermediate complete build was deliberately terminated after the low-memory
resume bug was found. It is not validation of the final code; all final results
above were obtained after that fix. Existing LVGL deprecation warnings remain.
Build: PASS. Host tests: PASS. Device tests: NOT RUN.
Unverified: M4-02 driver-release error propagation and final code audit; physical
memory/timing/display/Flash/audio/BLE/power and real SiliconFlow behaviour.
No device accessed, commit, push or publication.

Verified archive:
`build/firmware/dcdcf9ca1e86880b7e3085f357a708436bc74a2c576e921768434965ac377281/`.
Full-image SHA-256: `dcdcf9ca1e86880b7e3085f357a708436bc74a2c576e921768434965ac377281`.
Matching ELF SHA-256: `1fe2dd0265c374a37aa5269be6a0ee95973496bede28b534c41ad841b27463c3`.
ESP-IDF `v5.5.3`, app `5ddf0ee-dirty`; application 3415040 bytes in the
7274496-byte factory partition, merged image 3480576 bytes. Ignored local artifacts.

## 15. Driver ownership closure and requirement audit — 2026-10-28

The remaining M4-02 code now returns normal Wi-Fi stop/deinit failures, retains
unfinished ownership and retries release before reinitialisation. Provisioning
cannot start, and low-memory capture cannot retry, after a failed release.
Normal events publish state/address only for their captured session; tests
interrupt an IP callback with both suspend and suspend/restart.

Actual BLUFI tests exposed the symmetric exit path. Cleanup now waits for host
exit before freeing GATT, checks port release, waits for profile-deinit callback
before freeing BTC, and retains failed Wi-Fi/handler cleanup stages. The
callback acknowledges profile shutdown even after ordinary provisioning events
are disabled. An old STA config read is revision-checked against successful
phone writes; failed writes are not published. Negative/null/oversized credential
inputs are rejected. The inspected IDF 5.5.3 implementation queues profile
shutdown and its completion through BTC, which justifies that acknowledgement.

The M2 audit also reproduced an active recording hidden behind twenty old
acknowledged files in a one-row snapshot. Bounded copies now prioritise
capture/draft and pending entries before compatibility archives, without deleting
any file. Failed directory reads latch recovery protection; creation and both
upload/capture guards observe a failure found by the current scan. The wake
boot guard now publishes its deadline before atomically arming; actual boot
variants cover a held key and release before button initialisation.

### Requirement-by-requirement code evidence

The register contains 23 numbered items. The following audit checks their actual
implementation and relevant assertions rather than treating a transition table
or one narrow green test as proof of a whole feature. Firmware suites below are
registered in `tools/validate.sh`; server suites run in the final race/E2E gates.
All device/provider checks in the last column were deliberately deferred by the
user and are not counted as software test passes.

| ID | Implementation and executable evidence | Deferred acceptance |
| --- | --- | --- |
| M1-01 | Upload parser/publication and memo/job transaction; `memos_test.go` and `queries_test.go` cover malformed counts/durations, caps, concurrent replay and rollback | Real Flash/network interruption on device |
| M1-02 | Restricted device IDs and cross-device replay ownership; HTTP malformed/path/boundary tests | None for this code rule |
| M1-03 | Joined bounded worker pool, claim guards, explicit safe GC and rejected inactive config; worker/store/config tests and E2E | Live provider latency/operational observation |
| M1-04 | Original MEMO response and exact Range contract; HTTP/audio tests and real-process E2E byte comparison | Audible device playback |
| M1-05 | Mutation hub, bounded snippets/SSE, authoritative single-writer refresh; hub/HTTP and firmware SSE/events/API tests | Long physical sessions and dropped LAN |
| M2-01 | `.rec`/`.draft`/`.memo` ownership and save/cancel; real spool, recorder/uploader and actual app assertions | Power cycle before/after confirmation |
| M2-02 | Non-destructive mount/recovery, NVS preservation, priority scan and I/O guards; real-file and injected-error tests | Full/corrupt SPIFFS and physical power/Flash errors |
| M2-03 | Exactly three rejections per round, retry/delete, same UUID and legacy drain; policy/uploader/spool/app tests | Corrected dialog/retry/delete on device |
| M2-04 | Monotonic warning/cap plus 5000-frame limit; recorder and actual app cap/stop assertions | Measured 4:30/5:00 and audio duration |
| M3-01 | Five-row viewport, selected UUID and bidirectional cursors; browse/UI/API/app and server page tests | Physical corners and key feel |
| M3-02 | Async versioned UTF-8 windows, scrolling, cancellation, playback actions; text/API/UI/app and HTTP long-text reconstruction tests | Physical long text and audio navigation |
| M3-03 | Retained decoder/source pause and acknowledged stop; actual local/HTTP player fault tests and app actions | Audible frame boundaries, long remote pause |
| M3-04 | Side-effect-free drafts and confirmed NVS/audio commit; real settings faults and app save/cancel/retry tests | Actual volume and reboot persistence |
| M3-05 | Truthful status/notice, UUID dedup, copied network data, battery and bounded labels; actual UI/app/STA/BLUFI tests | Physical layout/font/battery output |
| M3-06 | Pinned source/converter/inventory/payload and OFL; static glyph/metric/license checks plus both regenerated payloads | On-device glyph rendering |
| M4-01 | Ownership coordinator and actual BLUFI cleanup/acks; pure, actual module and app failure/rejoin tests | Mini-program, BLE/radio re-entry |
| M4-02 | Checked startup/release, retained failed stages, old-session/config rejection, worker stop and cache ownership; STA/BLUFI/app/API/discovery/worker faults | SDK-internal blocking, heap pressure and real driver errors |
| M4-03 | Queue-only button/timer callbacks, startup rendering retry and common heartbeat; contract tests and actual continuous input segments | RTOS scheduling and visible refresh timing |
| M4-04 | Verified endpoints, generation invalidation, limits and queued manual discovery; API/discovery/uploader/app tests | Moved/unreachable LAN service |
| M4-05 | Capture/confirmation without IP and AP-loss branch; actual app plus recorder/spool/uploader checks | Physical offline-save-reboot-reconnect journey |
| M5-01 | Idle protection, entire wake gesture and ordered terminal shutdown; policy faults and held/released actual boot variants | Current, peripheral teardown and deep wake |
| M5-02 | Real application/BLUFI harnesses, worker faults, server race/E2E, static/build/archive gates | Consolidated board/provider regression |
| M5-03 | Per-capture encoder allocation and acknowledged release, existing heap/largest-block diagnostics; real recorder lifecycle assertions | Peak heap/largest block/stack and instrumented display/audio timing |

M0's scaffold is covered by application-only source registration, target/default
configuration, paired-document/repository checks, reproducible font assets and
the verified partition/merged-image gate. Startup and navigation use the custom
Voice Memo pages; baseline demo sources are reference/host-test material only.

### Final validation and software closure

| Check on the final code | Result |
| --- | --- |
| ESP-IDF 5.5.3 `./tools/validate.sh` | PASS: repository/host tests, firmware, partitions and merged-image verification |
| Linux/GCC static gate | PASS, including actual BLUFI, driver release, stale session/config, spool priority/I/O and wake-boot tests |
| Actual application and BLUFI ASan/UBSan | PASS, no sanitizer finding |
| `go test -race -count=1 ./...` | PASS |
| Real-process fake-ASR E2E | PASS: upload/idempotency/transcription, bidirectional history/versioned text, CRUD/retranscribe, safe GC and exact audio Range |
| `generate_memo_fonts.py --check` | PASS: both 16/20 px bitmap/metric/cmap payloads reproduced |
| Archive verification and root merged-image hash | PASS, matching manifest and ELF |
| Repository/bilingual checks and `git diff --check` | PASS |
| Device / real SiliconFlow tests | NOT RUN, explicitly deferred |

A read-only formatting inspection also found an unchanged baseline style
difference in `server/internal/domain/memo.go`; it was preserved and is not a
functional validation failure. No server source changed in this increment.

**Software completion audit: PASS for M0 and all 23 numbered code requirements.**
The user's continuous code-development objective is complete. Physical acceptance
is still open under each register item: Chinese pixels/corners and key feel,
record/save/reboot/retry/delete and full Flash, mini-program/BLE/LAN recovery,
long text and audible pause/streaming, peak heap/largest block/stack, current/deep
wake and real SiliconFlow requests. SDK-internal stop/blocking and real driver
failure behaviour remain board checks, not guarantees established by host mocks.
No device was accessed or flashed; no commit, push, release or publication ran.
This is software closure, not completion of the project's hardware acceptance.

Build: PASS. Host tests: PASS. Device tests: NOT RUN.
Unverified: the deferred board/instrument/provider checks above.

Verified final archive:
`build/firmware/4de2b0ae9eeb86311f587609bea920bce7a39b62ebefb40a67b26153015c3514/`.
Full-image SHA-256: `4de2b0ae9eeb86311f587609bea920bce7a39b62ebefb40a67b26153015c3514`.
Matching ELF SHA-256: `a2f977a9e2ef5f7acb43f247eb76bd06592d557f5dc1d78bbb374ee9b72d2cd2`.
ESP-IDF `v5.5.3`, app `5ddf0ee-dirty`; application 3416512 bytes in the
7274496-byte factory partition, merged image 3482048 bytes. Ignored local
artifacts; existing LVGL deprecation warnings remain.


## 16. Full terminal detail and playback following — 2026-11-09

The detail interaction supersedes the earlier collapsed/expanded navigation:
enter directly into full detail; OK plays/pauses/resumes, hold OK returns to the
list, and UP/DOWN apply and persist volume in 20% steps with hard 0/100% bounds.
They no longer select recordings or scroll text. Local waiting labels are shortened,
and a compact mono status/volume line leaves the footer for playback and return.
The existing phosphor-green terminal presentation is retained.

Text follows audio position proportionally to transcript byte length, including
bounded adjacent-window reads. Requests use server-provided UTF-8 boundaries;
pause freezes scrolling and replay returns to the beginning. There are no
segment timestamps, so this is approximate following. The player retains its last
position at completion; a new play resets it, and a newly opened detail does not
inherit another recording's progress. Read retries preserve existing text, and
failed volume persistence keeps the confirmed setting with transient feedback.

Validation: complete ESP-IDF 5.5.3 gate PASS (static/host, firmware, partition and
merged-image checks), final static rerun PASS, application ASan/UBSan PASS, and
archive verification PASS. Supplemental application assertions after the final
progress isolation fix also passed under ASan/UBSan; firmware compiled that fix.
Tests cover volume limits/failure, detail identity, following/window loading,
pause/replay, and local/remote completion position retention. No server code
changed; server race/E2E was not rerun in this increment. Object-model tests do
not prove pixels, audible volume, RTOS timing or physical acceptance.

Build: PASS. Host tests: PASS. Device tests: NOT RUN.
Unverified: terminal layout/glyphs, audible volume and save/reboot, automatic
scroll pacing with real audio/long text/network delays, physical regression.
One USB serial candidate was detected read-only; no port opened or device flashed.
No commit, push or publication occurred.

Verified archive: `build/firmware/75208c87a31fd2e602cfe3895399b20eb0f9e9410f1e1d861fc61e36ebec4d74/`.
Full-image SHA-256: `75208c87a31fd2e602cfe3895399b20eb0f9e9410f1e1d861fc61e36ebec4d74`.
Matching ELF SHA-256: `4b4c0493509828f27e6e2b3c089cd7a69f42b96056fdcfc0da57d1ae09dc40b9`.
ESP-IDF `v5.5.3`, application `5ddf0ee-dirty`; app 3420096 bytes in the
7274496-byte factory partition, merged image 3485632 bytes. Local ignored artifacts.


## 17. Deliberate inspiration capture — 2026-11-09

Accepted scope keeps the single transient slot, remote-server dependence and
held-to-record interaction for concise thoughts. No offline queue, continuous
recording mode or additional playback controls were introduced.

- A held OK spans backlight wake; a short wake only lights the screen. Deep-wake
  OK held through initialization enters normal one-second arming after readiness.
  Early release cancels, direction-key wake gestures stay consumed, and an ADC
  recheck using BSP-defined windows prevents capture after a lost release.
- Optional Autosave defaults off and brings settings to eight rows. A completed,
  closed draft returns to the list with three seconds to undo by OK, counted
  from the first successful render lock. Upload/sleep remain blocked until the
  ordinary save transition succeeds. Failed move/delete uses manual confirmation;
  recovery drafts never auto-upload, even when Autosave is enabled.
- WAIT/REC/SAVE separate preparation, capture and confirmation; the lamp only
  appears during capture. Frame-derived quiet/clipping hints use existing UI
  space, do not alter gain/delete recordings, and need acoustic acceptance.
  A full/unavailable spool reports a space warning before starting capture.
- Closed pending recordings may idle-sleep. Shutdown still joins network/audio
  workers, scans for active/draft files and fails closed on recovery errors.
  The locked safety recheck reuses the pre-lock scan under input-owner write
  ownership. Manual wake restores normal synchronization; no periodic wake added.

Validation: ESP-IDF 5.5.3 complete gate PASS, additional static gate PASS,
application and recorder ASan/UBSan PASS, archive/root-image identity PASS,
repository/bilingual checks and `git diff --check` PASS. Tests cover auto-save
persistence/failure/reboot, undo timing/render retries/save failure, manual draft
recovery, real input/state/UI actions, signal arithmetic/durations, closed-pending
versus unsafe spool checks, and held/short/released/direction deep-wake variants.
No server source changed; server race/E2E were not rerun for this increment.

Build: PASS. Host tests: PASS. Device tests: NOT RUN.
Unverified: real wake-to-audio latency and first-word capture, ADC/RTOS timing,
quiet/clipping thresholds and sound quality, footer/glyph geometry, Flash faults,
undo/save/power-cycle and deferred upload, standby current and wake/sync regression.
USB discovery found one serial candidate without opening it. No flash, commit,
push or publication occurred.

Verified archive: `build/firmware/99b1dddf52e735b61f89e6e34730fb898f1d9bb56e3172300ab99c33964f10bc/`.
Full-image SHA-256: `99b1dddf52e735b61f89e6e34730fb898f1d9bb56e3172300ab99c33964f10bc`.
Matching ELF SHA-256: `17ac3299aa712f30f359061cf6795d6deefb9c2f7d450fc58503ad8a2821b4bc`.
ESP-IDF `v5.5.3`, application `5ddf0ee-dirty`; app 3422032 bytes in the
7274496-byte factory partition, merged image 3487568 bytes. Local ignored artifacts.


## 18. Device flash and terminal column correction — 2026-11-09

The section 17 firmware (`99b1dddf52e735b61f89e6e34730fb898f1d9bb56e3172300ab99c33964f10bc`)
was explicitly approved and flashed to the single identified USB ESP32-C3/8 MB
candidate. Installed partition metadata matched exactly. Segmented bootloader,
partition-table and application writes passed hash verification, without writing
NVS, PHY or the spool. This was not a full-chip erase or firmware backup.
A bounded 20-second startup capture after reset matched the expected ELF prefix,
observed one boot and application idle, no spool mount failure, no error-tagged
lines and no crash markers. IP acquisition was not observed in that window.
The monitor closed normally. Raw logs remain local and were not committed.

The user then reported list time/status overlap on the device. Inspection found
that the bundled Unscii-16 advances 16 px, while the layout assumed 8 px. The
previous software pass did not establish correct hardware geometry. The fix uses
a reproducible, vertically doubled Unscii-8 with real 8 px advances / 18 px line
height. The original source/hash and MIT notice are retained under `assets/fonts`.
Time/text/duration now have bounded widths 40/95/40 at x=14/62/165, with 8 px gaps.
Numeric labels clip and text uses ellipsis. Shared battery/playback ASCII labels
also use the corrected narrow font. CJK fonts and application controls are unchanged.

Validation: complete ESP-IDF 5.5.3 gate PASS; actual generated glyph-advance and
host-fixture checks PASS; offline font reproduction PASS; application ASan/UBSan
PASS; archive/root-image/partition identity and repository/bilingual checks PASS.
A local bitmap comparison reproduced the old overlap and showed separation with
the new glyphs; it is a font/layout preview, not an LVGL or device screenshot.

Build: PASS. Host tests: PASS. Device tests: PASS for corrected-image flash/startup only; visual and functional acceptance pending.
Unverified: the corrected list, battery/status/playback raster output and prior
feature acceptance on hardware. The earlier image passed flash/startup checks but
failed visual acceptance by user report. The corrected image was subsequently explicitly approved and flashed to the same
identified device using verified segmented images after rechecking its partition
table. All three writes passed hash verification; NVS/PHY/spool were not written.
A fresh 20-second reset/startup capture matched the corrected ELF prefix, observed
one boot and application idle, with no spool-mount error, error-tagged lines or
crash markers. IP acquisition was not observed in that window. The monitor
closed normally; pixel separation still awaits the user's observation.
No commit, push or publication occurred.

Verified corrected archive: `build/firmware/bbca36295b5ff100283279d2fb9949428ea7cfc808f466a36a5b2f3fa8ed3a9e/`.
Full-image SHA-256: `bbca36295b5ff100283279d2fb9949428ea7cfc808f466a36a5b2f3fa8ed3a9e`.
Matching ELF SHA-256: `80121fa2c257056d197cf859e7b48b7312453c3617bfcf6df41de5cc65d80ef9`.
ESP-IDF `v5.5.3`, application `5ddf0ee-dirty`; app 3421296 bytes in the
7274496-byte factory partition, merged image 3486832 bytes. Local ignored artifacts.


## 19. Local SiliconFlow activation — 2026-11-09

The user selected this computer as the server and entered the credential directly
in the owner-only `server/.env.local`. The existing SiliconFlow adapter was used,
with the user's configured `Qwen/Qwen3-ASR-1.7B` model and the official origin.
No credential was printed, embedded in firmware or put in a command argument.
The private configuration and `server/data/` are now explicitly ignored by Git.

`scripts/run-server.sh` loads literal allowlisted settings without shell evaluation,
reuses Go cache setup, builds the server and runs it in `server/`. The example
configuration has no credential. Parser tests cover literal command-substitution
text, CRLF, quotes, missing files and errors that do not reveal values. The
provider request test now checks the exact HTTP route and actual RIFF/WAVE bytes.

The service is running on port 8080 with SQLite healthy and a zero-depth queue at
the final check. No boot/login daemon was installed; configuration is read at
startup, and `./scripts/run-server.sh` starts it again after a stop/reboot.
A roughly one-second synthetic-tone MEMO was uploaded through the local API,
decoded and submitted by the real background worker to SiliconFlow. It reached
`ready` with provider/model metadata and a trace identifier. Empty text is
expected for this non-speech fixture; this does not prove human speech accuracy.
The synthetic record was deleted after the check. No user's recording was used.

Validation: server build PASS; full `go test -race -count=1 ./...` PASS; isolated
real-process fake-ASR E2E PASS; literal-config parser tests PASS; real provider
call PASS; complete repository gate and archive verification PASS; final
repository/bilingual checks and `git diff --check` PASS. No firmware behavior
changed and no device was flashed in this increment. Hardware remains on the
section 18 image. Device upload/discovery and real spoken-text acceptance remain
unverified. No commit, push or publication occurred.

Build: PASS. Host tests: PASS. Device tests: NOT RUN for server/device acceptance.
Unverified: actual device LAN discovery/upload and human speech transcription.

Server binary: `build/server/memo-server`; SHA-256
`c39bbdd87166d74ea0a3469174af77e8405668b8133a0b38ad389f06675e9da0`.
The required full gate also retained a new local firmware archive (not flashed):
`build/firmware/7e1c3b3064166432741c1a8f8d698aa24abde07e0d0d703b72a9effd6320e447/`.
Full-image SHA-256: `7e1c3b3064166432741c1a8f8d698aa24abde07e0d0d703b72a9effd6320e447`.
Matching ELF SHA-256: `576f976480168b7fc3e3917ba8b95ac3f19280d93dca158b1f9f73429e51d18f`.


## 20. Device service discovery repaired — 2026-11-09

The user reported that the connected device could not find the local server.
HTTP was healthy on loopback and the computer's LAN address, and the firewall
was disabled. A real Wi-Fi mDNS probe exposed a multi-label instance containing
a `.local` suffix, a `.local.local.` SRV target, and a VPN IPv4 mixed with the
LAN address. These were observed wire values, not inferred from successful startup.

The publisher now normalizes the instance to one bounded UTF-8 DNS label and
registers an explicit ASCII FQDN and one matching IPv4/TXT address on a selected
active multicast LAN. Down, loopback, non-multicast, point-to-point and unusable
IPv4 candidates are excluded. `MEMO_MDNS_INTERFACE` provides an explicit override
and fails visibly when unusable. Loopback-only listeners no longer advertise,
preventing local E2E servers from replacing production DNS-SD records.

The owned server process was path-checked, gracefully restarted with the existing
private provider configuration/database, and verified again by a fresh Wi-Fi
query. The instance has the expected four DNS-SD labels, the host has a single
`.local.` suffix, and only the LAN IPv4 is published. LAN/loopback ping passed.
The user explicitly confirmed that the device now finds the server. No device
flash, firewall/VPN change, credential change or data deletion was performed.

Validation: complete repository gate and archive verification PASS; full Go race
tests PASS; isolated E2E PASS; config/parser and DNS wire/name/interface tests PASS;
repository/bilingual checks and `git diff --check` PASS. Interface/IP changes
still require restarting the publisher; automatic network-change tracking was
not added. No commit, push or publication occurred.

Build: PASS. Host tests: PASS. Device tests: PASS for service discovery, user-confirmed.
Unverified: speech transcription accuracy and other device acceptance from section 19.

Running server SHA-256: `571e279ee77afa4050995f6a595534dffcf75506adb310b0cdd3d60039f52c3e`.
Full-gate firmware archive (not flashed):
`build/firmware/0ebb5349a935b514150577583a2044223ed34af7a784cb93bca2a5956c2aa56a/`.
Full-image SHA-256: `0ebb5349a935b514150577583a2044223ed34af7a784cb93bca2a5956c2aa56a`.
Matching ELF SHA-256: `77c7115cea172902191bbf96bb4ca9474668e8f24d773a34c431ada477ceed36`.


## 21. Stable settings-address marquee — 2026-11-09

After confirming service discovery, the user reported the server-address marquee
in settings. Inspection of the pinned LVGL source showed that setting the same
long-text mode deletes its animation and clears the offset. Settings reconciliation
was doing that every refresh, preventing the address from completing a scroll.

Refresh now compares the actual label mode and only changes it on a selection
transition. Identical filtered text remains untouched. Outgoing hidden rows also
return to ellipsis, and selecting a row again starts its normal marquee. Other
settings, playback controls and transcript following are unchanged.

Host fixtures now model the mode setter's animation reset. Actual UI tests check
30 unchanged refreshes, visible and hidden selection changes, and retained offsets;
the actual application input-loop test checks the server address across repeated
heartbeat renders. Complete ESP-IDF 5.5.3 gate, application ASan/UBSan, archive and
root-image identity, repository/bilingual checks and `git diff --check` all PASS.
These tests establish setter/lifecycle behavior, not physical pixel timing.

Build: PASS. Host tests: PASS. Device tests: NOT RUN for this firmware.
Unverified: on-device uninterrupted server-address scrolling, text edges and return
selection. The same USB device was detected again; no flash or reset was performed
in this increment. The existing server keeps running with section 20's discovery
fix. No commit, push or publication occurred.

Verified firmware: `build/firmware/1c287764d5350965e39cb8ee629578bf4922c2c7db0a012dae44f1804e979deb/`.
Full-image SHA-256: `1c287764d5350965e39cb8ee629578bf4922c2c7db0a012dae44f1804e979deb`.
Matching ELF SHA-256: `03590cef80e13ebf3b90575ae6b7dcd27f53d0cc6b96d2d9ed2e5f6328ade2bf`.
ESP-IDF `v5.5.3`, application `5ddf0ee-dirty`; app 3421344 bytes in the
7274496-byte factory partition, merged image 3486880 bytes. Local ignored artifacts.

## 22. Single-line settings values and authorized flash — 2026-11-09

The user authorized flashing and requested single-line truncation for the
unselected server address. Settings values now have a fixed one-font-line
height (31 px) and 100 px width. LVGL dots mode can therefore truncate long
addresses instead of expanding to two lines; selection enables the continuous
marquee from section 21 within the same bounds. The host layout fixture now
respects explicit heights, and the actual UI regression checks the address
bounds after selection changes and layout.

Complete ESP-IDF 5.5.3 gate, archive/root-image identity, repository/bilingual
checks and `git diff --check` PASS. After explicit user authorization, the same
USB recorder's installed partition table matched the verified bundle. Segmented
writes at `0x0`, `0x8000` and `0x10000` passed all three hash checks; NVS, PHY and
the local spool were not written. No whole-chip erase or firmware backup was
performed. A bounded 20-second startup capture confirmed the matching ELF, one
boot, application idle, no spool-mount error and no crash/error markers. The
serial port was released. No IP event was observed during that bounded window;
this is not evidence of a network failure. Raw logs remain local.

Build: PASS. Host tests: PASS. Device tests: PASS for write verification and
startup only. Unverified: physical single-line ellipsis, selected continuous
scrolling, return selection and network reconnection. No commit, push or
publication occurred.

Verified firmware: `build/firmware/ae17c0062eae90b513d91ef5693f144de25e4e97d725d4dc5a8cc9b278c27132/`.
Full-image SHA-256: `ae17c0062eae90b513d91ef5693f144de25e4e97d725d4dc5a8cc9b278c27132`.
Matching ELF SHA-256: `1946ac6b67ab79ad3655d06b14c11959ad4553f67ee4328ca263ad21e9e969ec`.
Application 3421344 bytes; merged image 3486880 bytes. Local ignored artifacts.

## 23. Capture gesture and dedicated preparation — 2026-11-09

The user reported that cancelling preparation opened a memo, the ring overlapped
recording content, and capture looked static. A normal tap now stays on the list.
After 180 ms a dedicated preparation screen shows the ring and hold/cancel cues;
capture still starts one second after the original press. Committed release/click
tails are consumed until a fresh OK press. Callback timestamps, queued-input
handling and the current arming deadline protect delayed taps and stale timers.
Physical-ADC and full wake-gesture guards remain in place.

Capture now separates a large timer, twenty actual input-level history samples,
live input percentage and the existing quiet/clipping hints. Bright recent
samples move left on the existing heartbeat; quiet input is not simulated.
Confirmation hides history/input and retains the final duration and choices.
Existing fonts are reused; no additional worker, timer or asset was added.

Pure boundary/history tests, actual app regression, all deep-wake variants and
ASan/UBSan PASS. The complete ESP-IDF 5.5.3 gate, archive/root-image identity,
repository/bilingual checks and `git diff --check` PASS. USB discovery briefly
found no recorder, then found the same device again after build completion.
No flash or device reset was performed. This increment awaits user approval for
the new firmware; the section 22 image remains the last image flashed here.
No commit, push or publication occurred.

Build: PASS. Host tests: PASS. Device tests: NOT RUN for this firmware.
Unverified: physical tap/preparation timing, rounded-screen/glyph rendering,
input-history readability/latency, actual microphone levels and runtime free
heap/largest block with networking active.

Verified firmware: `build/firmware/1bfe489151f3f8c5bc7d0ea921ae90f425f52433c014eb9b8533d51022a5913d/`.
Full-image SHA-256: `1bfe489151f3f8c5bc7d0ea921ae90f425f52433c014eb9b8533d51022a5913d`.
Matching ELF SHA-256: `afd9fc1d362a4483cfc27a2961ccefdb42d7969546dfc49dfe32bf37bb8e8196`.
ESP-IDF `v5.5.3`, app 3422864 bytes in the 7274496-byte factory partition;
merged image 3488400 bytes. Local ignored artifacts.

## 24. Authorized capture-interaction flash — 2026-11-09

The user explicitly authorized flashing section 23's validated firmware.
Read-only USB discovery identified the same recorder with no serial-port owner.
The installed partition table exactly matched the verified bundle; segmented
writes of bootloader at `0x0`, partition table at `0x8000` and application at
`0x10000` passed all three hash checks. NVS, PHY and the recording spool were
not written. No whole-chip erase or original-firmware backup was performed.

A bounded 20-second startup capture confirmed the matching ELF, one boot,
application idle and no spool-mount failure, crash or error markers. No IP event
was observed during that window; this does not establish a network fault.
The monitor closed and released the port. Raw logs stay local. No source,
commit, push or publication changes occurred during flashing.

Build: PASS (section 23 complete gate). Host tests: PASS (including ASan/UBSan).
Device tests: PASS for write verification and startup only.
Unverified: cancel after the preparation screen without opening detail, next
fresh click behavior, one-second capture hold, actual history/glyph rendering,
save-confirmation layout and runtime memory with network/capture active.

Flashed archive: `build/firmware/1bfe489151f3f8c5bc7d0ea921ae90f425f52433c014eb9b8533d51022a5913d/`.
Full-image SHA-256: `1bfe489151f3f8c5bc7d0ea921ae90f425f52433c014eb9b8533d51022a5913d`.
Matching ELF SHA-256: `afd9fc1d362a4483cfc27a2961ccefdb42d7969546dfc49dfe32bf37bb8e8196`.
Application 3422864 bytes, merged image 3488400 bytes; ESP-IDF `v5.5.3`.

## 25. Unified phosphor terminal pages — 2026-11-09

The user requested Chinese recording/volume labels, removal of persistent key
legends, then authorized redesigning every page around the Fallout-inspired
retro terminal direction. The completed increment uses warm phosphor tokens,
a shared Chinese header/battery/rule and borderless reading/menu surfaces.
Four two-line list entries give summaries an independent 184 px line; metadata
cannot overlap pending text. Detail separates title, metadata, frameless text,
Chinese state/volume and compact transport. Settings has a focused editor;
provisioning uses a state hero; capture, preparation and decisions use distinct
layouts with retained save/delete choices and temporary feedback. Existing
buttons, capture protection, autosave, storage and server behavior are retained.

Actual pinned-LVGL RGB565 rendering of nine synthetic populated states was
visually reviewed. Real-engine bounds additionally checked list text/metadata,
capture caption/timer and confirmation feedback/options. This exposed retained
bottom alignment on the capture feedback label; its constructor now uses
absolute coordinates and host fixtures model persistent alignment. Battery
registration now covers all six persistent pages. Preview images and source/PNG
identities are local ignored artifacts at `build/ui-preview/`, including
`terminal-pages.png` and a manifest bound to the validated firmware. Their full
framebuffer exists on the host only; no firmware drawing buffer/task/font was
added. These captures are not device screenshots.

Complete ESP-IDF 5.5.3 gate, actual app/UI and pure-layout tests, all wake variants,
ASan/UBSan, glyph coverage/advance/kerning checks, archive/root-image identity,
preview source identity, repository/bilingual checks and `git diff --check` PASS.
The existing dependency lock pins LVGL `9.6.0~1` and was not changed. Delivery
USB enumeration found no target recorder. No flash or reset was performed;
section 24 remains the last firmware actually flashed here. No commit, push or
publication occurred.

Build: PASS. Host tests: PASS. Device tests: NOT RUN for this firmware.
Unverified: physical glyphs/rounded-edge clipping, phosphor brightness, marquee
speed, interaction timing, real audio-level feedback and runtime heap/largest
block with networking/capture active. Power on and reconnect by data-capable USB
before an explicitly authorized on-device test.

Verified firmware: `build/firmware/3ea90bc2debd1fcecb17cab0f514d845f70385c3bb886a3a5dfc5f5ee464a8d1/`.
Full-image SHA-256: `3ea90bc2debd1fcecb17cab0f514d845f70385c3bb886a3a5dfc5f5ee464a8d1`.
Matching ELF SHA-256: `e2472515363136e197f2db0409c35d72c4f6742ee91f9d25e18fe39dddc8d51f`.
Application 3422832 bytes in the 7274496-byte factory partition; merged image
3488368 bytes. Partition layout unchanged; preserve NVS/PHY/spool by checking
compatibility and writing matching components rather than the padded merged file.

## 26. Authorized terminal-redesign flash — 2026-11-09

The user powered on the recorder and explicitly instructed flashing section 25's
verified terminal redesign. Read-only discovery identified the same USB target
with no port owner. Its installed partition table exactly matched the verified
archive. Segmented bootloader (`0x0`), partition (`0x8000`) and application
(`0x10000`) writes passed all three hash checks; NVS, PHY and spool were not
written. No whole-chip erase or original-firmware backup was performed.

A bounded 20-second startup capture confirmed the matching ELF, one boot,
application idle, no spool-mount failure and no crash/error markers. Startup
reported 80632 bytes free heap, largest block 69632 bytes. These values
are startup observations, not capture-time or all-pages-active memory acceptance.
No IP event was observed during the window; this does not establish a network
fault. The serial monitor closed and released the port. Raw logs stay private
and local. No code, commit, push or publication changes occurred during flashing.

Build: PASS (section 25 complete gate). Host tests: PASS (including ASan/UBSan).
Device tests: PASS for write verification, firmware identity and startup only.
Unverified: physical page appearance, Chinese glyphs/corner clipping/brightness,
address marquee, navigation, capture cancellation and signal/choice display,
network reconnection and memory during real capture.

Flashed archive: `build/firmware/3ea90bc2debd1fcecb17cab0f514d845f70385c3bb886a3a5dfc5f5ee464a8d1/`.
Full-image SHA-256: `3ea90bc2debd1fcecb17cab0f514d845f70385c3bb886a3a5dfc5f5ee464a8d1`.
Matching ELF SHA-256: `e2472515363136e197f2db0409c35d72c4f6742ee91f9d25e18fe39dddc8d51f`.
Application 3422832 bytes, merged image 3488368 bytes; ESP-IDF `v5.5.3`.

## 27. Detail navigation, animated volume and post-blank sleep — 2026-11-09

Implemented all five requested refinements: one uniform dark separator replaces
the bright leader; detail direction holds switch previous/next recordings while
clicks retain volume; settings volume increases with UP/decreases with DOWN,
clamps at endpoints and has a 160 ms animated meter; playback fill is centred
inside its track; deep-sleep delay starts at the actual display-off transition.
Always-on display prevents automatic sleep. Defaults now mean 30 seconds to
blank plus five further minutes to sleep, with no NVS schema/layout change.

Pure navigation policy handles page edges and repeated local prefixes. The input
owner joins old audio and releases file ownership before switching, keeps the
current detail on asynchronous page failure and suppresses click tails begun
while loading. Volume retains draft/save semantics and animates from its actual
current width without heartbeat restarts; leaving the editor stops the animation.
Sleep editing explains post-blank timing or always-on suspension.

Complete ESP-IDF 5.5.3 gate, host state/protocol/layout/power tests, actual input
integration, all wake variants, ASan/UBSan, font checks, repository/bilingual and
`git diff --check` PASS. Tests cover playing/paused moves, both page directions,
first/last boundaries, player stop faults, local-file ownership, cancelled page
moves, a click spanning page completion, disabled/delayed/wake sleep timing and
volume limits/retargeting. Actual pinned LVGL rendering confirms 2 px symmetric
outer meter gaps and an 80-to-100% volume transition with midpoint width 140 and
final width 156; unchanged refreshes preserve completion. Local preview manifests
bind source/images and this firmware under ignored `build/ui-preview/`, including
refinement frames. No new application task or framebuffer was added.

Archive and root-image identity PASS. USB enumeration first found no target,
then the same recorder reappeared after verification. No flash/reset occurred
for this increment; section 26 remains the last actually flashed firmware.
No commit, push or publication occurred.

Build: PASS. Host tests: PASS. Device tests: NOT RUN for this firmware.
Unverified: physical key direction/hold timing, navigation and playback handoff,
meter motion/centering/contrast, actual post-blank deep-sleep timing and runtime
memory. New firmware awaits this iteration's explicit flash authorization.

Verified firmware: `build/firmware/902fcfe9e27de41359bc135b1410195b00499e3fdf5ed9605068deae5653c299/`.
Full-image SHA-256: `902fcfe9e27de41359bc135b1410195b00499e3fdf5ed9605068deae5653c299`.
Matching ELF SHA-256: `79c2fc8ad736f189bab4f9812e8bdb0ec8f6157c0fa2c66956a87754b2cac5b0`.
Application 3425072 bytes in the 7274496-byte factory partition; merged image
3490608 bytes. ESP-IDF `v5.5.3`; dependency lock and partition layout unchanged.

## 28. Authorized five-refinement flash — 2026-11-09

The user explicitly instructed flashing section 27's verified five refinements.
The same USB recorder was present and its port was not held by another process.
Its installed partition table exactly matched the bundle. Segmented writes at
`0x0`, `0x8000` and `0x10000` passed all three hash checks. NVS, PHY and spool
were not written; no whole-chip erase or original-firmware backup was performed.

A bounded 20-second startup capture confirmed the matching ELF, one boot and
application idle without spool-mount failure, crash or error markers. Startup
reported 79980 bytes free heap, largest block 69632 bytes. This does not
measure capture or all-pages-active memory. No IP event was seen in the bounded
window; it does not establish a network fault. The monitor closed and released
the port. Raw logs remain local/private. No source, commit, push or publication
changes occurred during flashing.

Build: PASS (section 27 full gate). Host tests: PASS (including ASan/UBSan).
Device tests: PASS for write verification, firmware identity and startup only.
Unverified: separator appearance, detail holds/page edges/audio handoff, volume
key direction/animation, playback-fill centering and actual display-off plus
sleep timing. These require user operation of the flashed recorder.

Flashed archive: `build/firmware/902fcfe9e27de41359bc135b1410195b00499e3fdf5ed9605068deae5653c299/`.
Full-image SHA-256: `902fcfe9e27de41359bc135b1410195b00499e3fdf5ed9605068deae5653c299`.
Matching ELF SHA-256: `79c2fc8ad736f189bab4f9812e8bdb0ec8f6157c0fa2c66956a87754b2cac5b0`.
Application 3425072 bytes, merged image 3490608 bytes; ESP-IDF `v5.5.3`.

## 29. Recording failure diagnosis and bounded page lifetimes — 2026-11-09

User repetition reproduced a recording failure and automatic reboot on section
28's firmware. The captured ELF prefix uniquely matches its verified archive.
Recording began with 79660 bytes free, largest block 43008 bytes; after opening
the 24640-byte encoder, task creation failed. Wi-Fi suspension freed total heap
to 123352 bytes, but encoder-first retry still failed to allocate the contiguous
28672-byte stack. Network recovery then reconfigured an already-running,
unsynchronised SNTP client, triggering the logged `sntp_setoperatingmode`
assertion and software reset. Matching symbols identify the panic entry;
the assertion itself establishes the failed SNTP invariant. Private logs remain
local. Initial successful recordings did not establish absence of this fault.

The user rejected persistent reservation of the entire recording budget and
chose early allocation of fixed resources plus release of page-owned resources.
Platform/input/audio/network/button initialisation now precedes page widgets.
Only one functional page exists: transitions reuse the fixed empty boot screen,
stop animations, unregister battery labels, delete the old tree and clear all
widget pointers before constructing the next. Application-owned selection,
drafts and detail data survive; render-lock retries do not replay side effects.
Recording joins SSE/upload before audio allocation, cancels on join failure or
physical OK release while joining, and restores workers after save/cancel/error,
remaining quiet during confirmation/undo. A low-memory retry can still release
Wi-Fi, after both workers acknowledge shutdown.

The recording stack is allocated before encoder/buffers/file preparation; its
worker waits at a start gate. Preparation failures delete the parked worker and
release acquired audio resources before retrying. No large audio reservation
is kept idle. Recorder/player/SSE/upload workers acknowledge cleanup and park;
their owner deletes them, synchronously reclaiming stack/TCB memory on this
single-core target instead of depending on idle-task self-deletion cleanup.
Timeouts retain ownership. SNTP start is separately idempotent even if time
synchronisation never completes.

Complete ESP-IDF 5.5.3 gate PASS. Host tests include 100 complete page/capture
cycles with constant live-widget count, network join/release faults, allocation
rollback and 100 recorder restarts, unsynchronised SNTP reconnects, and all four
wake variants. Application/wake ASan/UBSan PASS. Actual pinned LVGL rendered and
deleted all six page types over 660 transitions; screen count returned to the
boot baseline and host heap plateau stayed 11648 to 11648 bytes after warm-up.
This is host lifecycle evidence, not ESP32 heap/largest-block measurement.
Repository/bilingual checks and `git diff --check` PASS. USB discovery found the
same recorder and the port was released; no new flash, reset, commit or push.

Build: PASS. Host tests: PASS. Device tests: NOT RUN for this firmware.
Unverified: repeated physical recording/playback/page transitions, actual free
heap/largest block, network stop/start timing and visible/audible behaviour.
Section 28 remains installed; new firmware awaits explicit flash authorization.

Verified archive: `build/firmware/9e55385c980c6bce6213cbaec25289f13776d850b55486658b9e8662e7d0b255/`.
Full-image SHA-256: `9e55385c980c6bce6213cbaec25289f13776d850b55486658b9e8662e7d0b255`.
Matching ELF SHA-256: `fdd116f5d4c9a0eb31cdd239ced21aee2c5f59437865f64b438a38cdaab875d1`.
Application 3426208 bytes in the 7274496-byte factory partition; merged image
3491744 bytes. ESP-IDF `v5.5.3`; partitions and dependency lock unchanged.

## 30. Authorized page-lifetime firmware flash — 2026-11-09

The user explicitly approved flashing section 29's verified bundle. Read-only
discovery identified the same USB recorder, with no port owner. Its installed
partition table matched the bundle. Segmented writes at `0x0`, `0x8000` and
`0x10000` passed all three esptool hash checks; NVS, PHY and the spool partition
were outside the written ranges. No whole-chip erase or original-firmware
backup was performed.

A bounded 100-second observation, including an announced startup reset, matched
the ELF identity and captured one boot, application idle, no spool-mount failure,
no error tags and no crash markers. Startup free heap was 87412 bytes and the
largest block was 77824 bytes, versus section 28's startup record of 79980 and
69632 bytes. This single-startup comparison does not establish repeated-use
fragmentation or concurrency headroom. No capture/page-navigation actions were
observed during the window; the user was invited to exercise them. The monitor
closed and released the port. Raw logs remain local/private. No firmware source,
commit, push or publication change was made during this flash.

Build: PASS (section 29 complete gate). Host tests: PASS (including ASan/UBSan).
Device tests: PASS for write verification, matching identity and bounded startup.
Unverified: repeated recording/playback/page transitions, visible/audible behaviour,
capture-start latency and free heap/largest block across those actions.

Flashed archive: `build/firmware/9e55385c980c6bce6213cbaec25289f13776d850b55486658b9e8662e7d0b255/`.
Full-image SHA-256: `9e55385c980c6bce6213cbaec25289f13776d850b55486658b9e8662e7d0b255`.
Matching ELF SHA-256: `fdd116f5d4c9a0eb31cdd239ced21aee2c5f59437865f64b438a38cdaab875d1`.
Application 3426208 bytes, merged image 3491744 bytes; ESP-IDF `v5.5.3`.

## 31. Preparation animation covers resource handoff — 2026-11-09

The user reported no new faults on section 30's firmware, but observed a pause
after the preparation animation. Resource joining and codec/file allocation
previously began only at the one-second completion event. They now start once
the 180 ms preparation page is visible, outside the LVGL lock in the input owner.
The 72%-length arc repeats a 900 ms rotation while that work proceeds, including
past the minimum hold threshold. No preparation thread/stack was added, and no
large idle recording reservation is retained.

Recorder APIs separate preparation, gate release and prepared cancellation.
Preparation allocates stack/encoder/buffers/file without sampling or advancing
the recording timer. After at least one second and readiness, the owner checks
the physical key plus callback-side release epoch and opens the existing gate.
Release during preparation deletes the parked worker and discards its empty
file; release/repress callbacks queued behind blocking work invalidate the old
gesture even if ADC reads OK again. The input loop can retry a dropped timer
event. Preparation stays visible until actual gate release, with no explicit
blank-page handoff before backend work. Logs separate network-join and audio/file
preparation duration. Spool discard now returns close/removal failures.

Complete ESP-IDF 5.5.3 gate and archive verification PASS. Tests cover early
readiness without early sampling, preparation beyond one second, light gate
release without allocations, prepared cancellation/retry, queued release/repress,
discard errors, all four wake variants and existing 100 page/capture cycles.
ASan/UBSan application/wake/recorder checks PASS. Actual pinned LVGL confirms
rotation continues beyond one second and 660 create/render/delete transitions
return to the boot screen baseline; warmed host heap stayed 11648 to 11648 bytes.
Repository/bilingual checks and `git diff --check` PASS. The same USB recorder was
discovered read-only; no new flash/reset, commit or push occurred.

Build: PASS. Host tests: PASS. Device tests: NOT RUN for this firmware.
Unverified: physical transition smoothness, actual preparation timing and capture
onset, quick-release behaviour and runtime heap/largest block. Section 30 remains
installed; this increment awaits explicit flashing consent.

Verified archive: `build/firmware/6c5f708d206a6c58c6d6b0b886afd388f2ed8b716d2fe644e6dedccaa4afd97a/`.
Full-image SHA-256: `6c5f708d206a6c58c6d6b0b886afd388f2ed8b716d2fe644e6dedccaa4afd97a`.
Matching ELF SHA-256: `16ffd0e5862975f420308e7f6a9bbef26adb0741be36e8d6d91443d5095718c7`.
Application 3427040 bytes in the 7274496-byte factory partition; merged image
3492576 bytes. ESP-IDF `v5.5.3`; partition layout and dependency lock unchanged.

## 32. Authorized animated-preparation flash and capture — 2026-11-09

The user explicitly approved section 31's verified firmware. The same USB
recorder was present with no port owner; installed partition metadata matched.
Segmented bootloader/table/application writes at `0x0`, `0x8000`, `0x10000`
passed all three hash checks and reset. NVS, PHY and spool were not written;
no whole-chip erase or original-firmware readback was performed.

The announced reset and bounded 20-second serial window matched the ELF and
captured one clean boot with application idle, no mount/error/crash markers.
Startup free heap was 87592 bytes, largest block 77824 bytes. User operation
during observation produced one successful recording and explicit save:
preparation joined background workers in 699 ms and prepared audio/file in
71 ms, all while in ARMING. The completion action followed at 1005 ms after
press; recording was logged 25 ms later and the recording page 84 ms after
the completion action. The spool finalised 114 frames / 6840 ms / 16522 bytes,
then the user confirmed save and returned to the list. Return free heap was
93924 bytes, largest block 73728 bytes. This single capture does not establish
long-run stability, perceived animation smoothness or audio quality.
The monitor closed and released the port; raw logs remain local/private.

Build: PASS (section 31 complete gate). Host tests: PASS (including ASan/UBSan).
Device tests: PASS for segmented write/identity/startup and one capture/save.
Unverified: visual transition smoothness, quick-release cancellation, repeated
capture/playback/navigation, audible quality and long-run heap/largest block.
No firmware source changes, commit, push or publication occurred during flashing.

Flashed archive: `build/firmware/6c5f708d206a6c58c6d6b0b886afd388f2ed8b716d2fe644e6dedccaa4afd97a/`.
Full-image SHA-256: `6c5f708d206a6c58c6d6b0b886afd388f2ed8b716d2fe644e6dedccaa4afd97a`.
Matching ELF SHA-256: `16ffd0e5862975f420308e7f6a9bbef26adb0741be36e8d6d91443d5095718c7`.
Application 3427040 bytes, merged image 3492576 bytes; ESP-IDF `v5.5.3`.

## 33. Final review: deletion rollback, cleanup and dead paths — 2026-11-09

Reviewed active capture preparation/cancellation, worker ownership, page teardown,
playback, server deletion/job revocation and unreferenced legacy application paths.
Two injected regressions failed on the prior server code: job-removal failure
left a memo hidden despite failed deletion, and filesystem removal failure
returned 204 with no way to retry cleanup. Soft deletion and job revocation now
share a transaction; failed queue cleanup preserves the visible memo/job.
Known deleted IDs return their audio path for idempotent removal retries while
preserving the first deletion timestamp. HTTP removal failure publishes the
metadata deletion and returns 500; successful or already-absent cleanup returns
204, and unknown IDs still return 404. API tests exercise failure and retries.

The client now deletes rejected-upload panel/labels when the choice closes,
clears their pointers and restores rows in the same render. Discarding a recorded
capture propagates storage cleanup errors instead of always returning success.
Removed the unreachable download-before-play branch, Flash-cache path/calls,
unused ID-based playback/full-transcript/download helpers, unused settings and
provisioning wrappers, duplicate decoder close, empty action/render wrappers,
unreachable error state/three action hooks and an unreachable third retry delay.
All remaining declared actions are exercised; reusable board/demo references
and recording-data compatibility paths remain intact. No partition, wire-format,
credential, Chinese-font or saved-setting change was made.

Complete ESP-IDF 5.5.3 gate and verified archive PASS. Go vet, full Go race suite
and isolated fake-provider HTTP E2E PASS. ASan/UBSan application, four wake
variants, recorder and player checks PASS. Regressions cover deletion rollback,
retryable physical cleanup, original deletion time, dialog object release/row
restoration and recorder discard errors. Existing 100 navigation/capture cycles
and actual pinned LVGL 660 render/delete cycles still PASS; warmed host heap
remained 11648 to 11648 bytes. Repository/bilingual checks and diff whitespace
checks PASS. Read-only USB enumeration found no target recorder at handoff.

Build: PASS. Host tests: PASS. Device tests: NOT RUN for this firmware.
Unverified: physical recording/playback/navigation and failure recovery. Section
32 remains the last flashed firmware. The running local server was not restarted;
the new server binary is staged for application. No commit, push or publication.

Verified firmware: `build/firmware/2550ccd11889d3a5f6a1ac9718bfdd443f319c05110548d6b45b5f47495a6ecf/`.
Full-image SHA-256: `2550ccd11889d3a5f6a1ac9718bfdd443f319c05110548d6b45b5f47495a6ecf`.
Matching ELF SHA-256: `4d4eb659b547ef0f4c1c97cc869ab385142757eca650c38484fc0638c89f42ed`.
Application 3426192 bytes in the 7274496-byte factory partition; merged image
3491728 bytes. Partition table and dependency lock unchanged.
Staged server: `build/server/memo-server-review`; SHA-256
`879911fdf7f56863e7acbeff070bcd0790f6ec6fcba6f33053388e2aacb79acb`.
