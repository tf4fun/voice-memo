<p align="right">
  <a href="product-spec.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Voice Memo — Product Specification

Status: draft, agreed with the product owner on 2026-10-26. Implementation details
(font sizes, row density, copy) are deliberately left open and settled during
development.

## 1. Positioning

Voice Memo turns the FoloToy AI Passport into a **worn voice diary**: capture a
thought in a few seconds, and read it back later as text.

It records and revisits; it does not manage. There is no to-do state, no
checkbox, no completion, no inbox. An entry is a moment in time with an audio
recording and a transcript.

## 2. Core scenarios

| Scenario | What the user does | What the product must guarantee |
| --- | --- | --- |
| Quick capture | Holds OK for three seconds while walking, speaks for twenty seconds, releases | Saved immediately, uploads later on its own, never lost |
| Evening review | Scrolls the list back through today and yesterday, opens an entry, reads the text | Text is readable on a 240×320 screen, newest first |
| Long entry | Records a five-minute meeting recap | Duration is visible, a cap prevents runaway recordings |
| Listen again | Opens an entry and plays the original audio | Audio is the original recording, not a re-synthesis |
| Setup | Configures Wi-Fi from a phone, checks that the server was found | No keyboard is needed on the device |

## 3. Product form

One device, one cloud service. The device only records, browses, reads, and
listens; every text operation happens on the server, so the device never waits
for transcription.

Three screens, one settings area:

```text
List (home) --OK click--> Detail (text / audio) --OK hold--> back to list
    |
    |--OK hold 3 s----> Recording --OK click--> saved, back to list
    |
    +--UP or DOWN hold-> Settings --OK click--> the item's own sub-page
```

## 4. Screens and controls

### 4.1 List (home)

Newest first, grouped by day (Today / Yesterday / date). Entries that have not
reached the server yet stay in time order and show a small status dot.

| Input | Action |
| --- | --- |
| UP / DOWN | Move the selection |
| OK click | Open the entry |
| OK hold 3 s | Start recording |
| UP hold or DOWN hold | Open settings |

A short hint on the list states how to record and how to reach settings. Full
text is truncated at a character boundary and never breaks a multi-byte
character.

Before the device has been provisioned, the list shows a single empty-state line
pointing at settings instead of a blank screen.

### 4.2 Detail

Text on top, a playback bar below. A three-step cycle on OK, no focus
switching:

```text
collapsed --OK--> expanded --OK--> playing --OK--> paused --OK--> playing ...
     |
     +------------ OK hold: back to list ------------+
```

| State | OK click | UP / DOWN | OK hold |
| --- | --- | --- | --- |
| Collapsed (six lines, ends with an ellipsis) | Expand the full text | Previous / next entry | Back to list |
| Expanded | Start playback | Scroll the text | Back to list |
| Playing or paused | Pause or resume | Scroll the text | Back to list |

Expanding is a one-way action for the current visit; leaving the entry and
returning restores the collapsed state.

When **autoplay** is on (section 4.5), opening an entry starts playback
immediately and the first OK press pauses instead of expanding.

### 4.3 Recording

Large elapsed timer, a level bar, battery in the top-right corner.

| Input | Action |
| --- | --- |
| OK click | Stop, save, return to the top of the list |
| OK hold | Cancel and discard this entry |

No navigation is accepted while recording, so an accidental key cannot cut a
sentence short.

### 4.4 Recording arming

Holding OK from the list shows a ring that fills over three seconds; releasing
early cancels. The ring is the only feedback that a hold is long enough, and it
is what distinguishes "open this entry" from "start recording".

### 4.5 Settings

Up and down are both settings entries, so the gesture does not depend on which
end of the list the user is looking at.

| Row | Shows | OK click |
| --- | --- | --- |
| Network | `Connected <SSID>` or `Not connected` | Bluetooth provisioning (4.6) |
| Server | `Found <name> · reachable` or `Not found` | Discover again (4.7) |
| Volume | `40 / 60 / 80 / 100%` | Open the volume sub-page |
| Screen timeout | `Always on / 30 s / 1 min / 3 min` | Open the timeout sub-page |
| Sleep | `Never / 1 min / 5 min / 15 min` | Open the sleep sub-page |
| Autoplay | `On / Off` | Toggle in place |
| About | Firmware version, short device ID, pending uploads | Nothing |

Inside the settings list, UP and DOWN only move the selection. Inside a
sub-page the rule is uniform: **OK click saves and returns, OK hold cancels and
returns**.

- **Volume** is a single large number with an adjustment hint. Every step plays
  a short confirmation tone at the new level, so the setting is judged by
  listening rather than guessing.
- **Screen timeout and sleep** are option lists: UP and DOWN choose, OK saves.
  Defaults are 30 seconds and 5 minutes.
- **Autoplay** defaults to off: opening a diary entry in public should not
  suddenly speak.
- **About** is read-only.

### 4.6 Bluetooth provisioning

Opened from the Network row. The device stops normal Wi-Fi use and advertises
over Bluetooth; the phone supplies a 2.4 GHz network's SSID and password through
the companion WeChat mini program identified by
[the upstream provisioning guide](../development/engineering/wifi-provisioning.md#mini-program-name).

```text
┌──────────────────────────┐
│ Bluetooth setup           │
├──────────────────────────┤
│ In the mini program pick: │
│     BLUFI_MEMO            │
│                          │
│ ○ Waiting for phone…      │
│                          │
│ OK hold to cancel         │
└──────────────────────────┘
```

The advertised name must start with `BLUFI`, because the mini program lists only
such devices. The screen shows the exact advertised name so the user can match
it.

States shown in order: waiting for phone, receiving credentials, connecting,
success, or a failure reason (wrong password, network not found). On success the
device returns to settings with the Network row updated.

**The provisioning channel carries only the network name and password.** No
server address, token, or any other free-form parameter can be sent this way,
and the product does not pretend otherwise.

### 4.7 Server discovery

The server address is never typed on the device and never compiled in. The
device finds the server on the same local network by itself.

- The Server row is read-only: it shows the discovered name and whether the
  server answers.
- When nothing is found, the row reads `Not found`; OK click runs discovery
  again.
- Discovery failure is not an error state: the device keeps recording, keeps
  entries queued, and retries on its own.

## 5. Behaviour rules

1. **Saved before uploaded.** A finished recording is kept on the device first
   and uploaded afterwards, so a lost network connection never costs an entry.
2. **Silence is not an error.** Transcription happens in the background. An
   entry without text yet still opens, still plays, and says so.
3. **Status stays quiet.** Pending upload and pending transcription are a dot
   and, at most, one short label on the detail screen — not a task state.
4. **Five-minute cap.** The timer turns red at 4:30 and the entry stops and
   saves itself at 5:00.
5. **One entry, one moment.** No titles, tags, folders, or edits on the device.
6. **Battery is always visible.** If the gauge is unavailable, the corner shows
   an icon without a number rather than a fake value.
7. **Nothing sleeps mid-action.** Recording, playback, and Bluetooth setup keep
   the screen on and block sleep.
8. **Uploads outlive the screen.** With unsent entries the screen may turn off,
   but the device does not sleep until the queue is drained.
9. **Wake to the list.** Waking from sleep always returns to the list, never to
   the recording screen.

## 6. Not in this product

- To-do, checkboxes, completion or reminder states
- Editing, retitling, tagging, or deleting an entry on the device
- Typing a Wi-Fi password or a server address on the device
- A server address fixed at build time
- Factory reset from the device
- Search, favourites, export
- Multiple users or shared diaries
- Browsing server-side history while offline (only entries not yet uploaded
  remain visible)

## 7. Acceptance from the user's point of view

1. Holding OK for three seconds from the list starts a recording; releasing
   early cancels.
2. Holding either UP or DOWN opens settings.
3. In a settings sub-page, OK saves and returns; holding OK cancels and returns.
4. Changing the volume is audible immediately, before leaving the sub-page.
5. Screen timeout and sleep follow the chosen values, and neither happens while
   recording or playing.
6. Bluetooth provisioning works with the named mini program, and a wrong
   password shows a reason rather than silently failing.
7. The Server row shows the discovered server without any input, and recovers
   after a re-discovery.
8. A recorded entry appears at the top of the list before it has been uploaded
   or transcribed.
9. Pressing OK repeatedly on an entry walks collapsed → expanded → playing →
   paused without any other key.
10. Holding OK on an entry returns to the list from any state.
11. A five-minute recording stops by itself and is saved.

## 8. Deliberately undecided

Row density (compact timeline versus two-line cards), exact font sizes, and
wording of hints are implementation details. They will be decided against the
real screen and adjusted by feedback, without changing the model above.
