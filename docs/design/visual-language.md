<p align="right">
  <a href="visual-language.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Voice Memo — Visual Language

Implementation snapshot: 2026-11-09. The application uses a warm phosphor-green
terminal: square geometry, sparse rules, readable Chinese text and inverse
selection. Interaction follows the [product specification](../product/product-spec.md).
The redesigned application does not reuse the baseline demo shell.

## 1. Display and palette

The display is 240×320 RGB565, with a BSP-applied 30 px corner mask. Content uses
24 px horizontal safe insets; menu selection spans x=16…224. There are no shadows,
blur textures, simulated scan lines or page transitions.

| Token | Value | Role |
| --- | --- | --- |
| `TERM_BG` | `#061006` | Near-black screen and inverse text |
| `TERM_FG` | `#8CEC66` | Warm phosphor text and selected fills |
| `TERM_DIM` | `#4F8D3B` | Metadata and older input samples |
| `TERM_DARK` | `#1D3B20` | Quiet rules, frames and scrollbar tracks |

Status uses brightness, strip width and inverse video, not additional hues.
Permanent bottom key legends are absent. Selected choices, counts, countdowns,
input problems and actionable failures remain visible.

## 2. Shared structure and fonts

`memo_ui_header` creates a Chinese title at x=24/y=8, width 144 and line height
31, plus the right-aligned battery at x=184/y=16, width 40 and line height 18.
All six page types share cached SOC updates; only the current page is allocated.
Switching unregisters its battery label and deletes old widgets/animations before
building the destination on the fixed empty boot screen. A single 208 px low-brightness
rule at x=16/y=44 separates the title and content without a bright leading segment. `memo_ui_card` supplies
a default square frame; menu and reading surfaces explicitly remove its border.

| Role | Font and actual line height |
| --- | --- |
| Titles, summaries, parameters, statuses | Noto Sans SC Bold subset, nominal 16 px, line height 31 |
| Transcript, state hero and choices | Noto Sans SC Regular subset, nominal 20 px, line height 38 |
| Time, duration, battery and input percentage | Vertically doubled Unscii-8, 8 px advance, line height 18 |
| Capture timer | Montserrat 40, line height 48 |

Chinese fonts retain their configured fallback chains. The ASCII terminal font
must not draw Chinese. Static literal coverage, glyph advances, pair kerning
and 100% volume strings are checked against generated assets. Unsupported
dynamic characters display `?`. Sources, licenses and reproduction are recorded
in [assets](../../assets/README.md#voice-memo-fonts).

## 3. Reading and navigation

The list shows four borderless terminal entries, each 208×50 at x=16 and
 y=56 + 56×visible index. A summary has its own 184×31 viewport at row x=12/y=0.
Time and duration occupy separate 40×18 fields at row x=12/156, y=31. Ellipsis
never shares a vertical band with metadata. A 2 px status strip becomes 4 px
for failures; only the selected row is inverse. The scrollbar is x=228/y=56,
height 218. Counts or temporary feedback use y=280; the empty state is centred
at y=136 without duplicate footer text.

Detail has a Chinese title and a mono metadata line at x=24/y=55. The frameless
text viewport is x=24/y=80, 192×144, with 8 px text line spacing. A complete
31 px Chinese status/volume line at y=236 clears the 208×32 playback panel at
x=16/y=274. Playback is inverse while active. The numeric transport label and
56×8 progress track retain independent bounds. Temporary feedback replaces the
status line and normal status returns after the application notice expires.
UP/DOWN clicks control volume; direction holds switch adjacent recordings.
Playback still drives proportional scrolling. The 56×8 track has a 1 px border
and 1 px inner inset; the 52×4 maximum fill has equal 2 px outer gaps.

Settings use five borderless 208×38 rows on a 44 px pitch from y=56. Each row
has an indexed 88 px label and an independent 88×31 value viewport, separated
by 8 px. Only selected values scroll horizontally; unselected values truncate
on one line. Reconciliation changes long mode only on selection transitions.
The focused editor hides rows/scrollbar and shows a 192×116 frame at x=24/y=100,
a centred name, a quiet rule and a larger value. Volume and sleep editors expand
to 192×148 at x=24/y=92. Volume has a 160×12 track with a centred 156×8 maximum
fill; UP increases and DOWN decreases without wrapping. Its fill animates for
160 ms from the currently displayed width and repeated refreshes do not restart
it. Sleep shows that timing starts after blanking, or that always-on prevents
automatic sleep. Failures use y=280.

## 4. Capture and decisions

Preparation is a separate screen: a 132×132, 6 px ring at x=54/y=74, an OK cue
at its centre and a Chinese preparation state at y=224. Short taps stay on the
list; after 180 ms a 72%-length arc rotates every 900 ms, repeating until
resources are ready and the minimum one-second hold has elapsed. Backend
preparation runs behind this screen; it does not disappear into a blank screen
while resources are being prepared. Cancelling consumes the same gesture's click tail.

Capture has a Chinese title and 14 px recording square, a caption at y=48,
and a 192 px-wide timer at x=24/y=88. Twenty actual input-level samples move
left on the existing 200 ms heartbeat. Bars are 6 px wide on a 10 px pitch,
centred at y=188 with heights 2…56 inside y=160…216. The newest three are bright;
older samples are dim. Quiet input is not replaced by synthetic animation.
The Chinese input caption is y=224 and numeric percentage y=230. A rule at
 y=258 separates the normally empty warning area at y=270.

Confirmation hides live input/history, retains the final duration and shows a
Chinese save question or error at y=154. The borderless choice row is
x=24/y=224, 192×52; two 80×38 choices use inverse selection, with save initially
selected. Autosave instead uses the list's saved/undoable status and three-second
countdown, without a key legend.

Rejected uploads use one focused frame at x=16/y=57, 208×205 with current fonts.
Its title, three message lines and two 80×38 choices have independent bounds.
List rows and footer are hidden while this view is active. Retry remains the
initial choice; the UI does not change deletion or retry semantics.

## 5. Provisioning

The shared header names Bluetooth provisioning. A borderless 192×196 body at
x=24/y=60 contains the 20 px state hero, a rule at body y=48, an independent
single-line SSID marquee at y=64 and IP/detail at y=104 when SSID is present.
Other phone/network context starts at body y=64. The single line at root y=270
names the provisioning context, not a button gesture. Failed state is inverse.

## 6. Rendering and acceptance

Only the preparation ring, selected long values, actual input history and
500 ms recording indicator and changing volume fill move. No task, buffer or drawing dependency was
added to firmware. BSP keeps its single 20-line DMA buffer; all external LVGL
access remains under `bsp_lvgl_lock()`.

Host object tests cover geometry, persistent alignment, fonts, mode continuity,
choices and all six battery registrations. A local headless renderer uses the
pinned LVGL and actual application/font sources to inspect nine populated
states in RGB565. Its full framebuffer exists only on the host and is not part
of firmware. Preview captures use synthetic data and are Git-ignored under
`build/ui-preview/`; they are not physical-device screenshots. Real timing,
glyph appearance, corner clipping, brightness and runtime RAM remain device
acceptance checks tracked in [milestones](../project/milestones.md).
