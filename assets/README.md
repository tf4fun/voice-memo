<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Assets

This directory stores reusable fonts, images, music, sound effects, and battery profiles, organized by asset type.

Keep each asset in the matching subdirectory and document its destination, naming, integration method, and source/license. Do not mix binary assets with Markdown documentation.

## Fonts

Store reusable font files and generated font sources in `fonts/`.

- Use descriptive names that include the family, weight, size, and format when relevant.
- Document the source, license, character range, conversion command, and expected destination.
- Check Flash and internal-RAM impact before adding a font; the ESP32-C3 has no PSRAM.
- Do not commit fonts whose license does not permit redistribution.

### Voice Memo fonts

The application embeds `fonts/memo_font_16.c` (Bold, 16 px) and
`fonts/memo_font_20.c` (Regular, 20 px), derived from
[Noto Sans SC 2.004](https://github.com/notofonts/noto-cjk/tree/Sans2.004).
Copyright 2014–2021 Adobe. The font software uses SIL OFL 1.1; retain
[OFL.txt](fonts/OFL.txt) and [NOTICE.txt](fonts/NOTICE.txt) with redistributed font
assets. This license is separate from application code licensing.

[manifest.json](fonts/manifest.json) pins official source URLs/SHA-256, converter
`lv_font_conv` 1.5.2, output metrics and generated C payload hashes.
[charset.txt](fonts/charset.txt) records the original 4013 requested code points:
GB2312 level-one Han (3755), ASCII and punctuation/full-width forms. Both emitted
cmaps contain 4012 code points; unassigned U+FF00 has no source glyph and is not
part of the supported text set. Generated line heights are 31 and 38 px, not the
nominal glyph sizes. These metrics drive UI layout tests.

From the repository root, with Node/npm and Python available:

```bash
npm install --prefix bin/lvfont --no-audit --no-fund lv_font_conv@1.5.2
python3 tools/generate_memo_fonts.py --fetch --check
```

The source-font cache is ignored under `bin/fonts`; downloads are hash-checked
and existing mismatched files are preserved. `--check` writes temporary files
and reproduces bitmap/metrics/cmap payloads byte for byte, excluding the leading
CLI comment containing local paths. `--write` explicitly replaces generated C
files. Both use 4 bpp, uncompressed LVGL output. The maintained bitmap assets are
compiled through `main/CMakeLists.txt`; no OTF is loaded into device RAM.

All static UI literals are checked against both cmaps. Dynamic text uses the
selected size and Montserrat fallback; unsupported Unicode or invalid byte
sequences display `?`, preserving supported text and UTF-8 boundaries. Server
text and byte offsets remain unchanged. This host coverage policy does not
establish device rendering or peak RAM use.

## Images

Store reusable source images and generated display assets in `images/`.

| File | Dimensions and format | Use and source |
| --- | --- | --- |
| [`images/home.jpg`](images/home.jpg) | 3840 × 2160, JPEG | Product hero image embedded in both project README files to foreground AI Passport and its open, maker-oriented identity. |
| [`images/readme-hardware-specs.png`](images/readme-hardware-specs.png) | 2172 × 724, PNG RGBA | Optional technical infographic retained as a reference asset; it is no longer used as the homepage hero. Generated for this repository with the built-in image generation tool on 2026-09-17; the six labels and values were checked against the documented hardware contract. |
| [`images/logo-wordmark.png`](images/logo-wordmark.png) | 1648 × 336, PNG RGBA | Transparent black wordmark extracted from the repository's original `images/logo.png`; embedded in both project README files for light backgrounds. |
| [`images/logo-wordmark-dark.png`](images/logo-wordmark-dark.png) | 1648 × 336, PNG RGBA | White version of the extracted wordmark, used by the README `<picture>` element when GitHub is in dark mode. |

- Use descriptive names and document dimensions, pixel format, conversion steps, and destination.
- Prefer formats suitable for the 240 × 320 RGB565 display and account for Flash and internal RAM.
- Preserve editable sources where licensing permits, and record the source and license.
- Never commit device QR secrets, credentials, or personal data in images.

## Music and sound effects

Store reusable music and sound-effect sources in `music/`.

- Document the source, license, sample rate, bit depth, channels, conversion command, and destination.
- Prefer 16 kHz, 16-bit mono PCM when it matches the current BSP audio path.
- Check Flash and internal-RAM cost before embedding audio; stream or chunk long recordings.
- Do not commit media without redistribution permission.

## Battery profiles

[`battery/wanli-500mah-profile2-xil40x-20261020.txt`](battery/wanli-500mah-profile2-xil40x-20261020.txt)
contains the supplied CW2017 profile for the Wanli New Energy 500 mAh cell
(profile2, XIL40X, 2026-10-20). It is an unchanged copy of the project-supplied
file, retained at the maintainer's request for this firmware update. Its
original filename is recorded in the paired Chinese document. No separate
license was included in the supplied file.

The format is 80 lines of two hexadecimal digits, one byte per line in register
order. The BSP embeds those bytes in `components/bsp/src/bsp_battery.c`; the
host gate checks the source file and initializer for an exact match. Text-file
SHA-256: `7ea96eba4634f4495a37c25ffacd54c1deaa0693b162c892d671a2ba68460919`.
Decoded 80-byte SHA-256:
`fa7365671093ef449c5bdcfb4adb279e3d275f888109f50b6b2f21e9b96220d5`.
This is a fuel-gauge profile, not a charging configuration or a generic profile
for every 500 mAh cell; see the [hardware guide](../docs/hardware-design/AI_HARDWARE_DEVELOPMENT_GUIDE.md#9-cw2017-fuel-gauge).


## Terminal ASCII font

`fonts/memo_font_terminal.c` provides a real 8 px fixed advance and an 18 px line
height. It vertically doubles the bitmap rows of LVGL's Unscii-8, preserving
horizontal pixels and spacing. The bundled Unscii-16 has a 16 px advance and
cannot serve as an 8 px column font. ASCII U+0020..U+007F is included; Chinese
continues to use the existing Noto subsets.

Pinned input: `fonts/source/lvgl_unscii_8.c`, copied unchanged from the project's
LVGL component; SHA-256
`bd95b245eaea139aa7c4d9efbc130cc8eda4c11dc7a22b42048a7f0d814e29bf`.
The bundled LVGL source and derivative retain the MIT notice in
[`fonts/LICENSE.lvgl.txt`](fonts/LICENSE.lvgl.txt), separately from Noto's OFL.
The original source is LVGL `src/font/lv_font_unscii_8.c`; the component version
is pinned in `dependencies.lock`. Reproduce offline with
`python3 tools/generate_memo_terminal.py`; verify with `--check`. CMake compiles
only the derived font. Width checks read the actual generated advances; host
object tests do not establish on-device raster quality.
