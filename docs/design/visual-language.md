<p align="right">
  <a href="visual-language.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Voice Memo — Visual Language

Status: draft, 2026-10-26. Applies to the device UI described in the
[client design](../technical/client.md); it does not change any product
behaviour.

## 1. Geometry inherited from the hardware

| Fact | Value | Source |
| --- | --- | --- |
| Panel | ST7789P3, 240 × 320 portrait, RGB565 | `bsp_pins.h` |
| Visible shape | rounded rectangle, **30 px radius** | `BSP_LVGL_SCREEN_RADIUS` in `bsp_display.h` |
| Outside the mask | pure black, applied per flush | `bsp_display_rounding.c` |
| Inversion | panel requires `INVON`; colors are authored normally | `BSP_LCD_INVERT_COLOR` |

The mask is a hard edge: anything drawn within 30 px of a corner is clipped to
black. Two rules follow directly:

1. **Safe area.** All content lives inside an 8 px inset, i.e. x 8–232 and
   y 8–312. Cards never touch the screen edge, so no card is ever visually cut
   by the mask arc.
2. **Inner radii stay smaller than the screen radius.** Cards use 12–14 px. A
   card whose radius equalled 30 would read as a second screen outline instead
   of a sticker sitting on one.

## 2. Design language

Two references, deliberately combined:

- **Sticker-sheet neo-brutalism** — flat saturated fills, thick ink outlines,
  hard offset shadows with no blur, chunky rounded shapes, one loud accent per
  state.
- **Playful UI** — everything looks like something you could peel off and stick
  elsewhere: cards slightly overlap their own shadow, selection nudges the card
  up, the record dot breathes.

What that means concretely on a 240 × 320 screen: outlines carry the structure
(not color gradients), shadows are solid blocks of black offset by three pixels,
and every interactive thing is a card you can point at.

## 3. Tokens

### Colour

| Token | Hex | Use |
| --- | --- | --- |
| `INK` | `#141210` | borders, primary text, hard shadows |
| `PAPER` | `#F6F1E6` | page background |
| `CARD` | `#FFFFFF` | card fill |
| `MUTED` | `#7A7368` | secondary text, timestamps |
| `SUN` | `#FFC53D` | selection, pending attention |
| `CORAL` | `#E5483F` | recording, failures |
| `MINT` | `#2EC4B6` | completed, playing |

Five colours total. The panel is RGB565, so pick values with clear separation;
the pairs above were chosen to stay distinguishable after quantisation to 5-6-5.

### Shape and stroke

| Token | Value | Notes |
| --- | --- | --- |
| `RADIUS_CARD` | 12 px | list rows, settings rows |
| `RADIUS_PANEL` | 14 px | large panels: transcript, record card |
| `RADIUS_PILL` | height / 2 | play bar, option pills, status glyphs |
| `STROKE` | 2 px | every card and divider; 1 px disappears against this panel |
| `SHADOW` | 3 px, no blur, `INK` | drawn as an offset rounded rect behind the card |
| `PAD` | 10–12 px | inner padding of a card |
| `GAP` | 6 px | between rows |

### Type

| Role | Font | Size | Weight source |
| --- | --- | --- | --- |
| List row text, settings labels, hints | CJK subset | 16 px | generated from a **bold** face |
| Transcript body | CJK subset | 20 px | generated from a **regular/medium** face |
| Timestamps, duration, battery | Montserrat | 14 px | already enabled |
| Timer, big numbers | Montserrat | 28 px | enable additionally (~20 KB) |

Splitting the weight by size is the cheap trick that makes this look like
stickers without doubling the font budget: 16 px text is short and wants
punch, 20 px text is long and wants to be read. Both subsets keep the same
coverage contract described in the client design (GB2312 level-1 plus
punctuation and ASCII).

Icons come from LVGL's built-in symbol glyphs inside Montserrat, so there are no
icon image assets and no extra RAM.

## 4. Components

### Card

The only container in the UI. Fill, 2 px ink border, 12–14 px radius, plus a
solid ink block offset by (3, 3) behind it.

```text
        ┌──────────────┐
        │   content    │      ← white fill, 2 px ink border, radius 12
        └──────────────┘
           ██████████          ← solid ink block, +3 px x/y, same radius
```

It is implemented as a background object plus the card object, not as an LVGL
shadow: a blurred shadow would be redrawn on every frame and is the single most
expensive effect available on this chip. Two rounded rectangles cost nothing.

### Selected state

Selection does not change the fill to an outline: the card **lifts**. Fill
becomes `SUN`, the shadow grows from 3 px to 4 px, and the card translates up by
2 px. One accent, one gesture, instantly readable at arm's length.

### Status glyph

A 8 px circle at the end of a list row, three states, no text:

| State | Drawing |
| --- | --- |
| waiting to upload | 2 px `INK` ring, hollow |
| transcribing | ring with a `MUTED` dot in the middle |
| failed | filled `CORAL` circle |
| done | nothing — the absence of a glyph is the success state |

Using shape rather than colour means the states stay distinct for a
colour-blind user and for a 5-6-5 panel.

### Row

Height 40 px, pitch 46 px with the 6 px gap. Contents, left to right: time
(Montserrat 14, `MUTED`, 44 px wide), one-line snippet (CJK 16 bold, `INK`,
truncated with an ellipsis), duration (Montserrat 14, `MUTED`, right aligned),
status glyph.

### Level bar

Twelve 8 × 16 px rounded bars with a 4 px gap; the active count is driven by the
RMS of the current audio chunk. Discrete bars rather than a continuous meter,
because they read as stickers and cost twelve rectangles to draw.

## 5. Page layouts

All coordinates are inside the 8 px safe inset. Height budget: status bar
32 px (y 8–40), content y 40–288, hint bar 24 px (y 288–312).

### List

```text
┌──────────────────────────────┐
│ MEMO                   82%   │  status bar, 2 px divider under it
├──────────────────────────────┤
│ Today                        │  date header, CJK 16 bold
│ ┌──────────────────────────┐ │
│ │ 08:32 reviewed the test… │ │  row, 40 px
│ └──────────────────────────┘ │
│ ┌──────────────────────────┐ │
│ │ 07:15 buy milk, eggs  0:12│ │  selected: SUN fill, lifted 2 px
│ └──────────────────────────┘ │
│                              │
│  hold OK to record · hold ↓  │  hint, CJK 16, MUTED
└──────────────────────────────┘
```

Five rows fit between a date header and the hint bar. Scrolling a whole page
moves the selection to the new top row; the list never scrolls independently of
the selection, so the highlighted card is always visible.

### Detail

```text
┌──────────────────────────────┐
│ ‹ 08:32 · 1:24         82%   │  header, 36 px
├──────────────────────────────┤
│ ┌──────────────────────────┐ │
│ │ reviewed the test plan   │ │  transcript panel, radius 14
│ │ with Zhang at three. He  │ │  CJK 20 regular, line 28 px
│ │ will have the test env…  │ │  collapsed: 6 lines
│ │                          │ │
│ └──────────────────────────┘ │
│ ┌──────────────────────────┐ │
│ │ ▶   00:12 / 01:24  ▬▬──  │ │  play bar, pill, 48 px
│ └──────────────────────────┘ │
└──────────────────────────────┘
```

Collapsed shows six lines; expanded lets the panel grow to fill the space and
scroll. The play bar is a pill, not a card: the one deliberate shape difference,
so "this is the thing that plays" is obvious without reading.

### Record

```text
┌──────────────────────────────┐
│ ● REC                82%     │  dot pulses at 0.8 Hz
├──────────────────────────────┤
│        01:23                 │  Montserrat 28, INK
│   ▮▮▮▮▮▮▮▮▯▯▯▯               │  level bar, 12 bars
│                              │
│   OK stop · hold OK cancel   │  CJK 16, MUTED
└──────────────────────────────┘
```

The card here is the whole content area, so the shadow reads as the page itself
being a sticker. The timer turns `CORAL` from 04:30.

### Settings

```text
┌──────────────────────────────┐
│ Settings             82%     │
├──────────────────────────────┤
│ Network    Connected MyHome ›│
│ Server     NAS-01 · ready  › │
│ Volume     80%             › │
│ Screen     30 s            › │
│ Sleep      5 min           › │
│ Autoplay   Off               │
└──────────────────────────────┘
```

Rows are 40 px with 6 px gaps; seven rows do not fit, so the list scrolls with
the selection, exactly like the memo list. The value column is right-aligned and
`MUTED`; the label is `INK`.

Sub-pages replace the row list with a single centred card: the option pills
stacked vertically, the current one filled `SUN`. OK saves, hold cancels, as the
product document requires.

## 6. Motion

Cheap, purposeful, and never during audio.

| Moment | Motion | Cost |
| --- | --- | --- |
| selection moves | card translates 2 px up, 60 ms | one property animation |
| record dot | opacity 1.0 → 0.35 → 1.0, 800 ms, repeat | one property animation |
| panel expand/collapse | height 150 ms, ease-out | layout recalculation |
| page change | instant cut | none |

No scale or rotate transforms anywhere: both force a full software
re-render of the subtree on this chip, and the "sticker tilt" effect is not
worth the frame budget. Tilt, if ever wanted, must arrive as a pre-rendered
image, not as a live transform.

## 7. Rendering constraints

- **No blurred shadows.** Implemented as a second rounded rect (section 4).
- **No rotation or scaling.** See above.
- **No image assets for icons.** LVGL symbol glyphs only.
- **One card layer.** Nested cards are not a pattern here; if content needs
  grouping, use a divider inside the card.
- **Full redraws are bounded.** The draw buffer is 240 × 20; a scrolling list
  repaints only the affected rows.
- **Backgrounds are flat.** No gradients, no dithering patterns; they cost
  compute and muddy the palette after RGB565 quantisation.

## 8. Do and do not

| Do | Do not |
| --- | --- |
| Keep every card inside the 8 px inset | Let a card touch the 30 px corner arc |
| Use 2 px strokes everywhere | Mix 1 px and 3 px strokes on one page |
| One accent fill per state | Colour four cards to "add variety" |
| Let the shadow overlap the next card slightly | Blur the shadow |
| Bold 16 px for labels, regular 20 px for prose | Set long transcripts in bold |

## 9. Acceptance

1. No rendered pixel outside the 30 px mask is anything but black.
2. Every card sits at least 8 px from the screen edge.
3. Selected card is distinguishable at arm's length by fill, lift and shadow.
4. Status glyphs are distinguishable without colour.
5. Transcript text at 20 px shows no clipped descenders or overlapping lines.
6. No motion runs while recording or playing.
7. A page switch repaints without a visible white flash.
