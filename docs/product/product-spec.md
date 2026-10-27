<p align="right">
  <a href="product-spec.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Voice Memo — Product Specification

Implementation snapshot: 2026-10-27, based on the current `feature/voice-memo`
code. This document describes current behaviour. Delivery status, remaining
work and acceptance checks are tracked in the [milestones](../project/milestones.md).

## 1. Positioning

Voice Memo turns FoloToy AI Passport into a wearable voice diary: record a
thought, send it to a server for transcription, read the text and replay the
original voice. Entries have no titles, tags, folders or to-do states. The device
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

The home page draws five fixed cards with time, text and duration. Local pending,
rejected and legacy unresolved recordings come first, followed by up to 24 server entries. Server entries are
ordered newest first; there are no date-group headings. The selection can move
beyond the five drawn cards, but the visible window stays on the first five.

| Input | Behaviour |
| --- | --- |
| UP / DOWN click | Move selection, stopping at either end |
| OK click | Open the selected entry |
| OK press and hold | Show a ring immediately; begin recording when it fills after one second |
| UP or DOWN hold | Open settings |

Releasing OK before the ring fills abandons recording preparation; a short
click can then open the selected entry. If an earlier recording is still pending
upload or waiting for a rejection choice, a new recording is blocked and the page shows a short notice.

The footer shows the entry count or an empty-state message. Battery percentage
appears on this page; an unavailable reading is shown as `--`. An unsynchronised
time is shown as `--:--`.

Server rows show a transcript snippet when present, a no-transcript label for
`ready` entries with empty text, and a transcribing label otherwise. This also
means a failed transcription currently uses the transcribing label.

### 4.2 Detail and playback

Opening an entry immediately displays the expanded text panel and a playback
bar. Remote text is fetched on entry with a 512-byte destination buffer. When
text is absent or unavailable, the page displays a fixed message about the local
recording and connecting to the server; this message is also used for remote
entries without text.

| Input | Behaviour |
| --- | --- |
| First OK click | Play the original recording |
| OK click while playing | Stop playback, entering the pause state |
| OK click in the pause state | Play again from the beginning |
| OK hold | Stop playback and return to the list |
| UP / DOWN click | No visible scrolling in the current application |

Local pending, rejected and legacy unresolved entries play from device storage. Server entries stream directly
from the server without a local audio cache. Autoplay starts playback when an
entry opens; it defaults to off. Playback completion returns to expanded detail.

### 4.3 Recording

Hold OK throughout capture. The page shows a large timer, twelve level bars and
a recording indicator. Release OK to stop; at 4:30 the timer turns red, and at
5:00 the application requests a stop automatically. Display timing and the cap use monotonic wall time, checked every 200 ms;
the stored audio duration counts encoded samples.

### 4.4 Save/cancel confirmation

Stopping finalises the local file and opens a save/cancel sheet, with save
selected initially. UP or DOWN switches choices; OK confirms; holding OK
cancels. The five-minute cap opens the same sheet and does not auto-confirm it.

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

UP and DOWN move between seven rows; holding either returns to the list.

| Row | Display | OK click |
| --- | --- | --- |
| Network | SSID, connecting, unconfigured or disconnected | Enter Bluetooth provisioning |
| Server | Discovered `host:port`, searching or not found | No action |
| Volume | 0 / 20 / 40 / 60 / 80 / 100%, default 80% | Open value overlay |
| Screen timeout | Always on / 30 s / 1 min / 3 min, default 30 s | Open value overlay |
| Sleep | Never / 1 min / 5 min / 15 min, default 5 min | Open value overlay |
| Autoplay | On / Off, default off | Toggle immediately |
| About | `v0.1` | No action |

Inside a value overlay, UP/DOWN cycle choices. Each adjustment takes effect and
is written to storage immediately; persistence errors are not shown. Both OK click and OK hold close the overlay without
rolling values back, despite the current save/cancel hint. Volume changes apply
to playback without a preview tone. Timeout and sleep values are stored settings;
they do not currently trigger backlight changes or automatic sleep.

### 4.6 Bluetooth provisioning

The Network row enters BLUFI provisioning as `BLUFI_MEMO`. The companion WeChat
mini program is identified by the [provisioning guide](../development/engineering/wifi-provisioning.md#mini-program-name).
It supplies a 2.4 GHz SSID and password, with no server-address or token field.
Normal network workers are asked to stop before this mode starts.

The page shows Bluetooth/Wi-Fi states and, on success, SSID and IP. Success
currently leaves the result page displayed. The footer advertises OK-hold exit,
but that gesture has no effect; holding UP/DOWN changes internal state without
performing page teardown or returning visibly to settings. A failed Wi-Fi
connection reports failure to the phone and returns to a connection/waiting
state; the device does not provide specific wrong-password or network-not-found
copy.

## 5. Storage and connectivity behaviour

- A 1 MiB local partition holds transient recordings. Files use an 80-byte
  container header followed by Opus frames. Stopping capture writes duration
  and frame count into the header.
- On boot, complete packets from interrupted files recover to an unconfirmed
  draft; incomplete tails are truncated. Invalid or empty files are quarantined
  without automatic deletion. RAM audio not yet flushed is lost.
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
- SSE notifications update cached metadata, supplemented by polling. Firmware
  browsing does not follow the server's pagination cursor.

## 6. Visual presentation

The UI uses a warm paper background, white cards, dark outlines and solid offset
shadows. Selection uses a yellow fill and a small upward offset. Generated 16 px
and 20 px Chinese font subsets fall back to Montserrat. Recording controls and
some hints use English. The [visual language](../design/visual-language.md)
describes the implemented geometry and assets.
