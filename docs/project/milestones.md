<p align="right">
  <a href="milestones.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Voice Memo — Project Milestones

Updated 2026-10-27 against the current `feature/voice-memo` code. This is the
single place for implementation follow-up, delivery status and acceptance.
The [product](../product/product-spec.md), [client](../technical/client.md),
[server](../technical/server.md) and [visual](../design/visual-language.md)
documents describe current behaviour rather than a backlog.

## 1. Milestone overview

| Milestone | Current implementation | Delivery state |
| --- | --- | --- |
| M0 — Scaffold | Go service, application entry, manifests, fonts and custom partitions exist | Implemented; validation is reported per run |
| M1 — Server | Upload, original-container storage, metadata CRUD, ASR jobs, mDNS and SSE exist | Main path implemented; contract and hardening follow-up below |
| M2 — Capture | One-second arming, held capture, release/cap confirmation, Opus spool and local playback exist | Main path implemented; storage/upload ownership needs hardening |
| M3 — Browse and settings | Five list cards, expanded detail, settings and CJK font integration exist | Partial; navigation, text and settings semantics below |
| M4 — Network | Wi-Fi/SNTP, BLUFI, mDNS/TXT, upload, server streaming and SSE exist | Main path implemented; provisioning handoff and lifecycle incomplete |
| M5 — Acceptance | Wake-key boot guard, heap logs and host tests exist | Open; automatic power management and full acceptance are not complete |

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
| M2-02 | P0 | Implemented: stop on read/encode/write error, sync batches, recover complete packets, quarantine empty/invalid data, preserve ownership on timeout and block after recovery I/O failure | Host fault/recovery checks PASS; device checks remain for full SPIFFS, power interruption and real flash errors. Mount still auto-formats on mount failure: decide a safe first-use/corruption distinction before claiming general data preservation |
| M2-03 | P1 | Corrected implementation: three rejected attempts per round, retry/delete, same-ID retry, single-slot blocking through every unresolved state; old `.keep` is compatibility input only | Host tests pass for a fresh three-attempt round and legacy drain; new firmware device acceptance remains: retry again, successful upload/deletion releasing the slot, offline/reboot recovery and no new local archive |
| M2-04 | P1 | Implemented: monotonic wall-clock display/warning/cap; stored duration remains encoded samples | Host wall-clock check PASS; measure 4:30/5:00 timing, release-to-confirm and sample duration on hardware |

### M3 — Browsing, settings and visual fit

| ID | Priority | Evidence in current code | Work and acceptance |
| --- | --- | --- | --- |
| M3-01 | P1 | Selection exceeds five drawn rows; seven settings rows are fixed and extend to y=318; list cursor is ignored | Keep selection visible, fit settings within the rounded safe area and define history pagination. Test more than five and more than 24 records; visit every settings row |
| M3-02 | P1 | Detail always forces expanded; scroll actions have no executor case; transcript destination is 512 bytes | Align state/action/rendering semantics and support readable long transcripts. Decide the collapsed-entry policy explicitly; test long Chinese text, scrolling and navigation on each detail state |
| M3-03 | P1 | Pause calls stop; resume restarts at the beginning | Implement true pause/resume or explicitly choose stop/replay semantics and align state, label and hints; measure progress before/after interruption |
| M3-04 | P1 | Settings mutate and commit immediately; hold-cancel does not restore values; no preview tone | Resolve save/cancel semantics and make hints truthful; cover save/cancel, storage-write failure and reboot persistence and audible volume feedback if preview remains part of the chosen interaction |
| M3-05 | P2 | Failed transcript appears as transcribing; remote no-text message claims local storage; battery exists only on list; row copies/clipping lack complete UTF-8 handling | Align status/copy, battery placement and safe text clipping. Verify failed/empty/offline states, missing glyphs, long snippets and all page corners |
| M3-06 | P2 | Dialog font cmap/negative checks exist; source/license/reproducible generation records remain incomplete | Complete font provenance and broaden coverage to all dynamic/static UI; verify widget selection and glyphs on hardware |

### M4 — Network and worker lifecycle

| ID | Priority | Evidence in current code | Work and acceptance |
| --- | --- | --- | --- |
| M4-01 | P0 | Provisioning cancel action has no executor; success does not tear down BLE or restore normal workers; footer says OK hold but table uses UP/DOWN hold | Wire success/cancel/failure handoff, cleanup and truthful controls. Repeat phone provisioning, exit and reconnect; confirm list/settings return and upload/SSE restart |
| M4-02 | P0 | Partially implemented: acknowledged stop, retained timeout handles, non-blocking rejoin before restart, interruptible waits, shorter socket timeouts; input task owns heartbeat/state | Host upload/SSE/recorder ownership checks PASS. Cache/discovery ownership remains open; test slow real HTTP, repeated lifecycle and memory-pressure handoff on hardware |
| M4-03 | P1 | Implemented: input loop runs the 200 ms heartbeat across pages; arming callback only enqueues; UI waits until startup completes | Device acceptance remains for live list/playback/provisioning refresh through every navigation and save/cancel path |
| M4-04 | P1 | Server row is read-only with no rediscovery action; FOUND precedes HTTP probe; stale address is retained; all upload 4xx include 429 | Define rediscovery and retry classification, apply byte/duration limits consistently. Test moved/unreachable service, failed probe, 429/5xx/4xx and reconnect without losing the pending file |
| M4-05 | P1 | Offline capture was not exercised in the historical handoff | Disconnect the access point during capture, confirm/save, reboot and reconnect. Verify intact audio, one accepted server record and recovery of normal browsing |

### M1 — Server contract and operations

| ID | Priority | Evidence in current code | Work and acceptance |
| --- | --- | --- | --- |
| M1-01 | P1 | Implemented: nonempty exact packet count, declared/frame duration agreement and caps, unique partials, non-replacing audio publication and atomic memo/job insert | Go race tests PASS for malformed counts/duration, actual frame cap, concurrent same-ID replay and enqueue rollback. Upload remains structural; Opus decoding errors are handled during transcription |
| M1-02 | P1 | Implemented: trimmed device ID restricted to `[A-Za-z0-9_-]{1,64}` before filesystem use; cross-device UUID replay returns 409 | Traversal, malformed IDs, boundary length and legitimate MAC/named device cases PASS in Go tests |
| M1-03 | P2 | `gc` is a placeholder, retention/concurrency settings are parsed without their advertised runtime effect | Decide supported operational configuration, then implement or remove inactive options. Test orphan/partial cleanup, retention and configured worker count as applicable |
| M1-04 | P1 | Implemented: E2E compares original MEMO bytes and exact Range bytes/headers; canonical WAV claim removed from handler | Real-process fake-ASR E2E PASS; Go adapter/decode tests and race gate PASS |
| M1-05 | P2 | Worker SSE sends full transcript into a firmware parser sized 512 bytes; no replay; management mutations lack notifications | Bound event payloads or make client parsing robust, define reconciliation, and test long text, dropped events and management updates |

### M5 — Power, regression and final acceptance

| ID | Priority | Evidence in current code | Work and acceptance |
| --- | --- | --- | --- |
| M5-01 | P1 | Timeout/sleep only persist values; app has no power loop | Implement screen timeout/deep sleep with recording/playback/provisioning guards, pending-upload protection and wake-to-list; measure real power and verify wake-key release |
| M5-02 | P1 | Partial: real upload/recorder/SSE workers and real spool files now have fault/lifecycle coverage, in addition to pure helpers | Record exact final gate/artifact below; remaining application actions and consolidated hardware regression stay open |
| M5-03 | P2 | Encoder now allocates per capture and releases only after acknowledged stop; earlier heap observations remain one device session | Measure peak heap/largest block/stack with the new lifetime and repeat capture, streaming, reconnect and provisioning; inspect display/audio timing |

## 4. Validation and delivery rules

For implementation, run focused checks, then the complete gate with ESP-IDF
5.5.3 activated. Server tests are separate. macOS host tests need the Linux
wrapper because the shared gate uses GNU linker flags.

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
