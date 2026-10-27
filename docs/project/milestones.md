<p align="right">
  <a href="milestones.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Voice Memo — Project Milestones

Status: draft, 2026-10-26. Scope comes from the
[product specification](../product/product-spec.md), the
[server design](../technical/server.md), the
[client design](../technical/client.md) and the
[visual language](../design/visual-language.md).

## 1. Sequencing

Two rules decide the order:

1. **Server first.** It is the only piece that can be exercised without hardware,
   and every device milestone after the recording one needs something to talk
   to. A single binary with `curl` and a fixture file is a usable target by day
   three.
2. **Every milestone ends with something demonstrable.** No milestone is "half
   of a layer"; each one leaves a build that shows the increment, even if the
   other half is a stub.

```text
M0 scaffold ──► M1 server ──────────────┐
    │                                   │
    └────────► M2 record ──► M3 browse ──┴──► M4 network ──► M5 polish
```

M2 can run in parallel with M1; M3 depends on M2; M4 needs both M1 and M2.

## 2. Milestones

### M0 — Scaffold (1 day)

| Item | Deliverable |
| --- | --- |
| Server skeleton | `server/` module, config parsing, SQLite open and migration, `GET /health`, one passing test |
| Firmware skeleton | new `partitions.csv`, tuned `sdkconfig.defaults`, demo pages replaced by a `memo_app` that draws one empty card |
| Traceability | the three design documents linked from `README.md` |

**Exit:** `go build ./...`, `go test ./...`, `./tools/validate.sh --static` and
`./tools/validate.sh --firmware` all pass. The device, if flashed, shows a single
card and nothing else.

**Demo:** a firmware that boots into an empty memo screen and a server that
answers `/health`.

### M1 — Server usable end to end (3–4 days)

| Item | Deliverable |
| --- | --- |
| Container | parser, validation rules, rejection codes |
| Audio | Opus decode to WAV, header/duration cross-check |
| Storage | memos and devices tables, file layout, soft delete |
| API | upload (idempotent), list with cursor, detail, audio with Range, PATCH, DELETE, re-transcribe |
| Jobs | `fake` transcriber, backoff, crash recovery |
| Discovery | mDNS advertises `_voicememo._tcp` |

**Exit:**

- `curl` script completes upload → list → detail → audio → PATCH → DELETE against
  a fixture container, with the expected status code at each step.
- A repeated `X-Memo-Id` yields one row and one job.
- Killing the process mid-transcription and restarting re-runs the job.
- Decoding a 5-minute container stays under 200 ms on the development machine.

**Demo:** upload a real recording from the laptop with `curl`, read the transcript
from `GET /memos`, play the WAV back.

### M2 — Device records and spools (3–4 days)

| Item | Deliverable |
| --- | --- |
| Logic | `memo_state`, `memo_text`, `memo_time` plus their host tests |
| Recording | record page, Opus encode, 4 KB batching, spool append, header rewrite on stop |
| Persistence | SPIFFS mount, boot scan, `.memo` / `.sent` / `.bad` naming |
| Limits | 5-minute cap with the 04:30 warning |

**Exit:**

- Hold OK for three seconds, record, stop: a `.memo` file appears with a valid
  header whose frame count and duration agree.
- Power-cycling the device preserves the file and lists it after boot.
- A five-minute recording stops by itself.
- Host tests cover every state transition, truncation case and backoff step.
- The key-wake guard wiring returns with the input task, and its contract test
  (removed with the baseline demo entry point in M0) is reintroduced.
- No audible glitch during recording while the screen updates.

**Demo:** record a memo, reboot, see it still there, open it and play it locally
(the player can be finished here even though network playback lands in M4).

### M3 — Browsing and reading (3–4 days)

| Item | Deliverable |
| --- | --- |
| Fonts | `lv_font_conv` subsets, coverage test with a negative case, fallback descriptor |
| List | date grouping, selection, status glyph, battery, hint bar |
| Detail | collapsed/expanded text, play bar, local playback wiring |
| Settings | volume, screen timeout, sleep, autoplay, about — with the sub-page rules |
| Style | the card system and palette from the visual language |

**Exit:**

- On-device screens match the specified layouts: nothing outside the 30 px mask,
  every card at least 8 px from the edge.
- Chinese text renders with no boxes on the device for the acceptance corpus.
- Selection, expand and collapse work with OK alone; hold always returns.
- Volume changes are audible immediately from the sub-page.

**Demo:** a diary you can read and re-listen to, entirely offline, with real
Chinese typography.

### M4 — Network (3–4 days)

| Item | Deliverable |
| --- | --- |
| Wi-Fi | STA connect, credentials in NVS, reconnect policy |
| Provisioning | BLUFI over NimBLE, started from settings, torn down afterwards |
| Discovery | mDNS query with SRV/A and an `ip=` TXT fallback, `/ping` and `/server` probe, limits adopted |
| Upload | uploader task, idempotent retry, `.sent` / `.bad` marking |
| Live updates | SSE subscription (`/api/v1/events`) so a finished transcription appears without polling |
| Playback | the stored Opus container is downloaded and played locally; streaming straight from the server is still open |
| Time | SNTP, timezone from settings |

**Exit:**

- A fresh device can be provisioned with the named mini program and finds the
  server without any input beyond the Wi-Fi credentials.
- Record → upload → transcribe → read → play works on the device.
- Pulling the network mid-recording loses nothing, and the upload completes
  after reconnecting.
- A rejected upload is marked `.bad` and is not retried.

**Demo:** the complete product loop on hardware.

### M5 — Polish and acceptance (2–3 days)

| Item | Deliverable |
| --- | --- |
| Power | screen timeout, deep sleep rules, wake to list |
| Degradation | every row of the client degradation table implemented and seen on screen |
| Hardening | heap high-water measured and recorded; stack watermarks trimmed |
| Handover | measured numbers and remaining unverified items in the delivery report |

**Exit:**

- The product acceptance list passes end to end on hardware.
- `./tools/validate.sh` passes; `go test ./...` passes.
- Heap high-water during recording with Wi-Fi up is recorded and has margin.
- The delivery report separates Build, Host tests, Device tests and Unverified.

**Demo:** acceptance walkthrough.

## 3. Dependencies and parallelism

| Milestone | Needs | Can run in parallel with |
| --- | --- | --- |
| M0 | — | — |
| M1 | M0 | M2 |
| M2 | M0 | M1 |
| M3 | M2 | M1 |
| M4 | M1, M2 | M5 (partially: power work needs only M3) |
| M5 | M3, M4 | — |

## 4. Effort

| Milestone | Days | Confidence |
| --- | ---: | --- |
| M0 | 1 | high |
| M1 | 3–4 | medium: provider integration and Range handling are the unknowns |
| M2 | 3–4 | medium: encoder CPU and flash stalls only show on hardware |
| M3 | 3–4 | medium: Chinese font generation and layout are iterative |
| M4 | 3–4 | low: BLE provisioning and mDNS on real networks are the riskiest work |
| M5 | 2–3 | medium |
| **Total** | **15–19** | one person, including hardware debugging |

If only a narrow slice is funded, M0 + M1 + M2 already deliver something real: a
device that records reliably and a server that stores and transcribes. Browsing
on the device (M3) is what turns it into the product.

## 5. Per-milestone definition of done

Every milestone, without exception:

1. Code committed on `feature/*` with a message describing the increment.
2. Host tests for any new pure logic, and the full gate run at the end:
   `./tools/validate.sh --static`, `--firmware`, `go test ./...`.
3. Documentation updated where behaviour changed, English and Chinese kept in
   step.
4. Delivery report separates `Build`, `Host tests`, `Device tests`,
   `Unverified`, naming exactly which hardware checks were skipped.
5. A one-paragraph note in the existing project notes: what was accepted, what
   is pending, and the exact build identity.

## 6. Risks by milestone

| Risk | Hits | Mitigation |
| --- | --- | --- |
| Provider rejects our WAV (sample rate, header, size) | M1 | the provider adapter is one file; test with a real key early in M1, not at the end |
| mDNS does not resolve on the user's router | M4 | discovered during M4 with the real network; the settings page already reports it, and the fallback discussion is recorded |
| BLUFI mini program cannot see the device | M4 | advertise the exact `BLUFI_` name the reference uses; test with the real phone in M4 |
| Encoder starves the UI on the single core | M2 | complexity 0, merged codec task, simplify the record page before touching the encoder |
| Chinese font too large or missing glyphs | M3 | generation is scripted and the coverage test fails loudly; the 16 px list font drops first |
| Heap exhaustion with Wi-Fi plus codec | M4, M5 | the tuning in the client design, and a measured high-water gate in M5 |

## 7. Explicitly out of scope

Deletion and editing on the device, offline browsing of server history, search,
OTA, device authentication or TLS, multi-device accounts, a web UI. Each is noted
in the design documents where it would attach, so it does not get rediscovered
as a surprise.

## 5. Current status

Snapshot taken while M4 is being closed out. "Verified" means seen on the
device with these logs, not merely compiled.

| Area | State | Evidence |
| --- | --- | --- |
| Record → spool → reboot | verified | 170 frames, 10.2 s, file survives a power cycle |
| Playback of a local memo | verified | decoded from the spool, audible |
| Bluetooth provisioning | verified | BLUFI handshake, WPA2 association, 192.168.31.116 lease |
| mDNS discovery | verified | PTR then `ip=` TXT, server reached on port 8080 |
| Upload | verified | 201, server row reaches `ready` with a transcript |
| List, detail, transcript | verified | rows and transcript pulled from the server |
| SSE updates | verified | uploaded → transcribing → ready within two seconds |
| Server limits adopted | verified | `max_memo_ms=300000` read at startup and enforced before upload |
| Recording with the network up | flashed, not yet confirmed on the screen | 28 KB stack + 24.6 KB encoder against a 73 KB largest block |
| Arming ring animation | fixed, not yet confirmed on the screen | value range was 0..100 while the animation fed it milliseconds |
| Upload rejected → `.bad` | code exists, not exercised | needs a deliberate 4xx |
| Recording while offline | not exercised | needs the access point switched off mid-session |
| Streaming playback | not implemented | the container is downloaded first, then played |

Heap, measured on this device: 231 KB free after boot, 100 KB once Wi-Fi is up,
84 KB with the uploader and the SSE subscriber running, largest contiguous
block 73 KB. The Opus encoder takes 24.6 KB, not the 40 KB assumed earlier.

