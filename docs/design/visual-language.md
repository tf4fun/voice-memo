<p align="right">
  <a href="visual-language.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Voice Memo — Visual Language

Implementation snapshot: 2026-10-27. Geometry and presentation described here
come from the current UI sources. Interaction is documented in the
[product specification](../product/product-spec.md); visual follow-up and
acceptance are tracked in the [milestones](../project/milestones.md).

## 1. Display and common primitives

The display is 240×320 portrait RGB565. BSP applies a 30 px rounded-screen mask
and fills outside it with black. An 8 px layout inset does not by itself prove
that corner content is safe from clipping; coordinates below are implementation
values, not measured layout acceptance.

`memo_ui_common` creates flat rectangles, labels and cards. A card consists of
two objects: an ink-filled offset rectangle and a white face with a 2 px border.
Visibility is applied to both. There are no blurred shadows or live rotation/
scaling effects. The baseline demo visual shell is not used.

## 2. Tokens

| Token | Value | Use |
| --- | --- | --- |
| `VM_PAPER` | `#F6F1E6` | Page background |
| `VM_CARD` | `#FFFFFF` | Card fill |
| `VM_INK` | `#141210` | Text, outlines, solid shadows |
| `VM_MUTED` | `#7A7368` | Secondary text |
| `VM_SUN` | `#FFC53D` | Selected fill and arming-ring background |
| `VM_CORAL` | `#E5483F` | Record indicator, warning/error, arming ring |
| `VM_RADIUS_CARD` | 12 px | Card corners |
| `VM_RADIUS_PANEL` | 14 px | Detail/settings panels |
| `VM_STROKE` | 2 px | Card outline |
| `VM_SHADOW` | 3 px | Shadow offset |
| `VM_INSET` | 8 px | Layout inset |

At 240 px width a standard card face is 221 px wide (`240 - 2×8 - 3`).
Selection immediately sets the fill to yellow and moves the face up 2 px;
list-row shadows move up 1 px. Settings selection moves the face only.

## 3. Typography

| Role | Current source |
| --- | --- |
| Labels, list snippets, settings, hints | `memo_font_16`, Noto Sans SC Bold subset, 4 bpp |
| Transcript | `memo_font_20`, Noto Sans SC Regular subset, 4 bpp |
| Times, duration, battery | Montserrat 14 |
| Recording timer | Montserrat 40 |
| Save/cancel choices | Montserrat 20, English labels |

Generated uncompressed sources live in `assets/fonts/memo_font_16.c` and
`memo_font_20.c`. Their generator headers record source/font options and symbol
sets (GB2312 level-1 characters, ASCII and punctuation). Writable descriptor
copies attach Montserrat 14/20 fallback for labels/body, including LVGL symbols.
Detail text is checked for missing glyphs and logged by code point.

## 4. Components

List cards are 40 px high on a 46 px pitch, with time left, snippet at x=52
within the card, and duration right. Status currently uses text, not the planned
dot glyph system. Labels do not have a complete fixed-width ellipsis layout.

The recording level meter contains twelve 12 px-wide rectangles on a 16 px
pitch. Their initial height is 20 px at y=190; the recording timer callback
changes the bar display based on the RMS-derived level.

The playback bar is a 48 px pill with symbol, position/duration and a short
progress track whose width ranges from 8 to 74 px when duration is known.

## 5. Page geometry

| Page | Current layout |
| --- | --- |
| List | Header at top, divider y=34; five rows at y=44 + 46×index; footer and battery label |
| Record | REC indicator, timer at top offset 70; 132×132 arming arc at offset 46; twelve bars; release hint |
| Record confirmation | Card at x=20, y=210, height 84, with save/cancel text |
| Rejected upload | List overlay at x=18, y=42, size 201×236 with the current font metrics; side-by-side 20 px choices, 16 px time/duration/message/hint; retry selected initially |
| Detail | Header; transcript starts y=44; expanded panel height 190; 48 px playback pill above footer |
| Settings | Seven 36 px rows at y=42 + 40×index; footer; centred 96 px-high value overlay |
| Provisioning | Title, divider y=34, panel at y=48 with height 176; state/detail text and bottom hint |

The list has five fixed row objects and no moving viewport. Settings also uses
fixed row positions without scrolling; its seventh row reaches y=318, overlapping
the footer/corner region. Detail marks its text panel scrollable, but button
scroll actions are not executed by the application. The app opens detail in its
expanded geometry. Battery is drawn only on the list.

The rejection overlay uses `memo_rejection_layout`, a pure calculation driven by
actual font line heights: 31 px for the 16 px subset and 38 px for the 20 px
subset. It reserves three message lines, a horizontal choice row and a hint,
rather than treating nominal font size as line height.

## 6. Motion and rendering

The arming ring animates from 0 to 100 over 1000 ms. Page switches are immediate.
Selection offsets and detail geometry are set directly, without the earlier
proposed selection/panel animations. The recording dot is static; recording
progress/level updates come from the 200 ms input-loop heartbeat.

The BSP uses a single DMA draw buffer of width × 20 lines (9600 bytes at
240 px RGB565). LVGL uses the system heap through `CONFIG_LV_USE_CLIB_MALLOC`;
the retained 24 KB pool setting is not an allocated LVGL pool in this mode.
UI calls outside the LVGL task use `bsp_lvgl_lock()`.
