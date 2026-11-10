<p align="right">
  <a href="product-spec.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Voice Memo — Product Specification

Implementation snapshot: 2026-11-09, based on the current `feature/voice-memo`
code. This document describes current behaviour. Delivery status, remaining
work and acceptance checks are tracked in the [milestones](../project/milestones.md).

## 1. Positioning

Voice Memo turns FoloToy AI Passport into a wearable voice diary: record a
thought, send it to a server for transcription, read the text and replay the
original voice. Capture is deliberately held and brief, intended for refining an
inspiration into a concise thought. Entries have no titles, tags, folders or to-do states. The device
has no editing or deletion interface for server history.

## 2. Deployment

The device connects to a server on the same LAN. The server stores history and
calls a hosted transcription service; a fake transcriber also supports local
development. The device itself does no speech recognition.

## 3. Navigation

```text
List --OK click--> Detail --OK hold--> List
  |
  +--hold OK for 1 s--> Record --release / 5-minute cap--> Save / cancel
  |
  +--UP or DOWN hold--> Settings --UP or DOWN hold--> List
```

## 4. Screens and controls

### 4.1 List

The home page uses four two-line terminal entries as a moving viewport. Local pending,
rejected and legacy unresolved recordings come first, followed by one server page
of up to 24 entries. The selected row stays visible. Server records are newest
first; moving past a page boundary requests the adjacent server page. Failed
requests retain the current page. There are no date-group headings.

| Input | Behaviour |
| --- | --- |
| UP / DOWN click | Move selection; at the page boundary load newer/older history when available |
| OK click | Open the selected entry after page loading completes |
| OK press and hold | Keep short taps on the list; after 180 ms show a separate preparation ring; capture starts once resources are ready and the hold reaches one second |
| UP or DOWN hold | Open settings |

Releasing OK before 180 ms remains a normal click and can open the selected
entry. Once preparation has begun, release cancels and consumes the same
gesture's trailing click; it never opens detail or confirms a recording.
The next fresh OK press restores normal click actions. Any unresolved local recording blocks new
capture and remains visible with its pending/rejected state.

An intentional OK hold can span backlight wake and start the usual one-second
arming interval; a short wake gesture only lights the screen. On deep wake, an OK
held through startup arms once the application is ready. Release before completion
cancels, and UP/DOWN wake gestures remain consumed. Actual startup latency still
requires a device measurement.

The footer shows current-page selection/count, loading/error or empty state.
Every page displays battery percentage (`--` when unavailable). An unsynchronised
time is `--:--`; duration is still displayed. Each summary has its own line,
with time and duration on a separate metadata line. Server rows distinguish waiting,
transcribing, failed and ready-without-text when no snippet exists.

### 4.2 Detail and playback

Detail opens directly with the full text panel. Text is requested asynchronously, retaining
one 2 KiB window, so input does not wait for the server. Local entries say they
are pending/rejected and can be played; remote empty, waiting, failed, missing
and offline states have their own messages. A remote entry never claims that
its original recording is stored locally.

| Input | Behaviour |
| --- | --- |
| OK click (autoplay off or playback finished) | Play the original recording |
| OK click while playing | Pause at the current position |
| OK click in the pause state | Continue the same source from the paused position |
| OK hold | Stop playback, cancel text reading and return to the list |
| UP / DOWN click in detail, playing or paused | Raise/lower volume by 20%, clamped at 0/100%; apply and save the setting |
| UP / DOWN hold in detail, playing or paused | Open the previous/next recording, including adjacent server pages; do not wrap at the ends |

Switching stops and joins the old audio source before releasing its file. Page
loading keeps the current detail visible; failed page moves retain that recording.
A gesture started during loading cannot accidentally play the newly loaded entry.

Text follows playback proportionally to audio position and transcript byte length;
pause freezes scrolling, replay starts at the beginning, and adjacent 2 KiB windows
load automatically. This is approximate following without word/segment timestamps.
The compact terminal status line displays Chinese transfer/text state and a
Chinese volume caption with percentage. Local
upload-waiting labels use a short two-character status. Volume save failures keep
the confirmed level and show an error. Read failures retry with bounded backoff.

Windows end on UTF-8 boundaries. If text is edited during reading, the device
restarts from the new version rather than mixing versions. Loading/failure leaves
already-read text available. Unsupported characters are displayed as `?` while
the server retains the original text.

Local pending, rejected and legacy unresolved entries play from device storage.
While local playback owns a file, upload cannot remove that source; a successful
in-flight upload defers deletion until playback has joined. Once upload releases
the transient file, the detail reads/plays the same ID remotely. Server audio
streams without a local cache. Autoplay starts playback on entry (default off),
and playback completion returns to full detail.

### 4.3 Recording

Hold OK throughout capture. A dedicated preparation page contains only the
ring, OK cue and preparation status. The moving ring covers network shutdown,
codec/buffer/file preparation and any wait beyond the minimum one-second hold.
The recording task remains gated, without sampling or starting its timer, until
the same held gesture is confirmed ready. Release cancels prepared resources;
queued release/repress callbacks cannot accidentally commit the old gesture.
Capture replaces it with a large timer,
a scrolling history of twenty real input-level samples, a live input percentage
and recording indicator. This is an amplitude history, not a PCM waveform.
Release OK to stop; at 4:30 the timer inverts, and at
5:00 the application requests a stop automatically. Display timing and the cap use monotonic wall time, checked every 200 ms;
the stored audio duration counts encoded samples.
Preparation, recording, save, capture and live input captions are Chinese,
matching the other pages. The recording lamp is visible
only in capture. Digital PCM hints report low input after three seconds below
RMS 80, or persistent near-full-scale clipping (at least 1% samples for 180 ms).
These are visual hints, not calibrated acoustic levels or automatic gain control.
Normal input clears them. A known full spool blocks capture with a space warning;
write/read failures retain recoverable data. No sound cue contaminates capture.

### 4.4 Save/cancel confirmation

Stopping finalises the local file, hides live input/history and opens a
save/cancel sheet beneath the final duration, with save
selected initially. UP or DOWN switches choices; OK confirms; holding OK
cancels. The five-minute cap opens the same sheet and does not auto-confirm it.

The optional Autosave setting defaults off. When enabled, a successful new
capture returns to the list with a three-second OK-to-undo window. The interval
starts after the first successful render lock; the closed draft stays ineligible
for upload and sleep during that window. OK removes only that draft. At expiry
the normal save operation commits it for upload; persistence/removal failure
returns to manual confirmation. Recovered or interrupted drafts always require
a manual choice, regardless of this setting. No extra copy or queue is created.

Save makes the local draft eligible for upload and returns to the list. Cancel
removes only the draft. Failed file operations retain the choice and report an
error. Capture and unconfirmed drafts block upload across reboot. After an
interruption, complete stored frames return to save/cancel confirmation; invalid
or empty data remains quarantined for a user decision. Unflushed audio cannot be
recovered. Upload success deletes the local file. Every unresolved recording
continues to occupy the single processing slot.

HTTP 4xx rejection is tried at most **three times total**, including the first
request. After the third rejection, automatic upload stops and the list shows a
retry/delete dialog with the recording time and duration. It waits until the
user returns to the list rather than interrupting another page or action.
UP/DOWN changes the choice and OK confirms. Retry is the default; holding OK
also retries. Retry starts a fresh round of at most three rejected attempts for
the same memo ID. Only an explicit delete choice removes the rejected recording;
file-operation failure leaves the file and dialog intact for another choice.

Local storage is one processing slot, not an archive. Until the recording has
uploaded successfully or been explicitly deleted, another recording is blocked,
including after choosing retry and across reboot. Undecided rejected files reopen
the choice even offline. The firmware creates no new kept-file archive. Legacy
`.keep` files from the superseded build are treated as unresolved: they also
block capture and offer retry/delete. If several legacy files already exist,
they are preserved and drained one at a time before allowing a new recording.

### 4.5 Settings

UP and DOWN move through a six-row viewport covering all nine settings;
holding either returns to the list.

| Row | Display | OK click |
| --- | --- | --- |
| Network | SSID, connecting, unconfigured or disconnected | Enter Bluetooth provisioning |
| Server | Discovered `host:port`, searching, waiting for network or not found | Queue rediscovery and API verification |
| Volume | 0 / 20 / 40 / 60 / 80 / 100%, default 80% | Open value overlay |
| Brightness | 10–100% in 10% steps, default 100% | Open preview/value overlay |
| Screen timeout | Always on / 30 s / 1 min / 3 min, default 30 s | Open value overlay |
| Sleep | Never / 1 min / 5 min / 15 min, default 5 min | Open value overlay |
| Autoplay | On / Off, default off | Toggle immediately |
| Autosave | On / Off, default off | Toggle after a successful settings commit |
| About | `v0.1` | No action |

Inside a value overlay, UP/DOWN change a draft without writing storage or applying
volume. Volume uses UP to increase and DOWN to decrease, clamped at the
endpoints, with a smooth fill that survives unchanged refreshes. OK saves the selected value; hold cancels and restores the saved display.
Write/commit failure keeps the overlay and draft open with a failure hint for
retry or cancel. Autoplay changes immediately only after a successful save;
failure leaves its confirmed value. Volume applies on successful save, without a
preview tone. Brightness uses UP/DOWN to increase/decrease by 10%, clamped to
10–100%, with immediate PWM preview and the same animated meter. Cancel restores
the saved brightness; successful save persists it for boot and wake. A failed
save retains the preview/draft for retry or cancel. Editing prevents idle sleep. Timeout and sleep settings control idle
backlight blanking and deep sleep. A short gesture after blanking only wakes the screen; a deliberate OK hold
continues into arming. Recording/decisions, playback including pause, provisioning and held
keys keep it on. Closed pending recordings may sleep; active capture, unconfirmed drafts,
recovery errors and the undo window block sleep. Network workers must acknowledge
stop before checking files. A second safety check reuses that file scan while
the input owner retains exclusive writing access. No periodic wake is configured;
after manual wake the normal network pipeline resumes synchronization. Deep-sleep wake returns to the list; a still-held OK enters arming after
startup, while other wake gestures remain consumed.

The sleep delay starts at the actual display-off transition, after the screen
timeout. Defaults therefore mean 30 seconds to blank and five further minutes
to sleep. Always-on display prevents automatic sleep; wake/activity restarts the
sequence, and delayed polling cannot skip the display-off wait.

### 4.6 Bluetooth provisioning

The Network row enters BLUFI provisioning as `BLUFI_MEMO`. The companion WeChat
mini program is identified by the [provisioning guide](../development/engineering/wifi-provisioning.md#mini-program-name).
It supplies a 2.4 GHz SSID and password, with no server-address or token field.
Normal network workers are asked to stop before this mode starts.

The page shows Bluetooth/Wi-Fi state. Holding UP/DOWN cancels, cleans up and
returns to settings. After obtaining IP and a two-second reporting interval,
normal networking resumes. Fatal module/startup failures use the same cleanup
path; failed cleanup retains resources and retries before normal Wi-Fi resumes.
Wi-Fi connection failures are reported to the phone and return to waiting.
The footer names the provisioning context without a permanent key legend.

## 5. Storage and connectivity behaviour

- A 1 MiB local partition holds transient recordings. Files use an 80-byte
  container header followed by Opus frames. Stopping capture writes duration
  and frame count into the header.
- On boot, complete packets from interrupted files recover to an unconfirmed
  draft; incomplete tails are truncated. Invalid or empty files are quarantined
  without automatic deletion. RAM audio not yet flushed is lost.
- Mount failure preserves existing Flash; only a verified entirely erased
  recording partition is automatically initialised. NVS failures do not erase
  saved settings or Wi-Fi credentials.
- With stored Wi-Fi credentials, the device reconnects automatically and starts
  time synchronisation. No server address is entered or compiled in: discovery
  uses mDNS on the local network.
- Successful upload removes the local file. Three HTTP 4xx rejections (including
  429) leave a visible `.bad` file waiting for retry/delete. Retry returns the same
  file to `.memo` and resets its three-attempt budget; it does not free the slot.
  Only upload success or explicit deletion frees it. Transport/discovery/5xx
  failures retain the pending recording without consuming the rejection budget.
- Rejection counting is in RAM for the current round/startup; `.bad` and legacy
  `.keep` persist across reboot and open retry/delete without automatic deletion.
- Server metadata is cached in RAM. A failed refresh keeps that cache, so some
  earlier rows can remain visible offline, but their text/audio still need the
  server. There is no persistent offline archive of server history.
- SSE notifications request a fresh authoritative list, including after reconnect,
  edits and deletion; polling supplements this. Firmware follows both history
  cursors while retaining one page.

## 6. Visual presentation

The UI is a monochrome phosphor-green CRT terminal: black background, square
panels with 1 px lines and no shadows; selection is inverse video. Generated 16 px
and 20 px Chinese font subsets fall back to Montserrat. Static text is checked
against both fonts; unsupported dynamic characters display `?`. Recording choices
and primary controls use Chinese, with physical OK labels and technical names retained. The [visual language](../design/visual-language.md)
describes the implemented geometry and assets.


Permanent bottom key legends are removed from the list, detail, settings,
preparation/capture and rejection views. Existing gestures still work. Live
states, selected save/delete choices, counts, autosave countdown and actionable
error feedback remain visible. Normal capture has no release instruction; quiet
or clipping input still shows feedback. Detail uses the freed area for transcript
space and shows temporary feedback in its status line.
