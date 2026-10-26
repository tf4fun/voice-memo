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

## 3. Product form

One device, one cloud service. The device only records, browses, reads, and
listens; every text operation happens on the server, so the device never waits
for transcription.

Three screens and one overlay:

```text
List (home)  --OK click-->  Detail (text / audio)  --OK hold-->  back to list
    |
    +--OK hold 3 s-->  Recording  --OK click-->  saved, back to list
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

A short hint on the list states how to record. Full text is truncated at a
character boundary and never breaks a multi-byte character.

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
returning restores the collapsed state. Keeping a single expanding action keeps
the interaction predictable.

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

## 5. Behaviour rules

1. **Saved before uploaded.** A finished recording is kept on the device first
   and uploaded afterwards, so a lost network connection never costs an entry.
2. **Silence is not an error.** Transcription happens in the background. An
   entry without text yet still opens, still plays, and says so.
3. **Status stays quiet.** Pending upload and pending transcription are a dot
   and, at most, one short label on the detail screen — not a task state.
4. **Five-minute cap.** The timer turns red at 4:30 and the entry stops and
   saves itself at 5:00.
5. **One entry, one moment.** No titles, tags, folders, or edits.
6. **Battery is always visible.** If the gauge is unavailable, the corner shows
   an icon without a number rather than a fake value.

## 6. Not in this product

- To-do, checkboxes, completion or reminder states
- Editing, retitling, tagging, or deleting an entry on the device
- Search, favourites, export
- Multiple users or shared diaries
- Browsing server-side history while offline (only entries not yet uploaded
  remain visible)

## 7. Acceptance from the user's point of view

1. From the list, holding OK for three seconds starts a recording; releasing
   early cancels.
2. A recorded entry appears at the top of the list immediately, before it has
   been uploaded or transcribed.
3. After some time the entry shows its text; opening it shows the text and a
   working playback bar.
4. Pressing OK repeatedly walks collapsed → expanded → playing → paused without
   any other key being needed.
5. Holding OK always returns to the list, from every state of the detail screen.
6. A five-minute recording stops by itself and is saved.
7. Unplugging the network while recording does not lose the entry.

## 8. Deliberately undecided

Row density (compact timeline versus two-line cards), exact font sizes, and
wording of hints are implementation details. They will be decided against the
real screen and adjusted by feedback, without changing the model above.
