<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Voice Memo on FoloToy AI Passport

A voice diary firmware for the [FoloToy AI Passport](https://github.com/FoloToy/ai-passport)
wearable: hold one button to record, let the server transcribe it, and read it
back later as text, with the original audio one press away.

This repository is a derivative of `FoloToy/ai-passport` and keeps its history
and upstream remote, so baseline updates can still be merged. Application work
happens on `feature/voice-memo`; `main` stays a clean upstream baseline.

Status: product design agreed, implementation not started. See
[the product specification](docs/product/product-spec.md).

## Repository layout

| Path | Tracked | Purpose |
| --- | --- | --- |
| `main/` | yes | Application: pages, state machines, tasks |
| `components/bsp/` | yes | Board support: display, audio, buttons, battery |
| `docs/product/` | yes | Product specification and, later, design and milestone documents |
| `scripts/` | yes | Reusable development and validation scripts |
| `tools/` | yes | Upstream repository gate and firmware verification |
| `bin/` | **no** | Local ESP-IDF installations, toolchains and Python runtime |
| `.dsh/` | **no** | Installed `passport-*` AI skills for this workspace |
| `reference/` | **no** | Checked-out reference projects |

`bin/`, `.dsh/` and `reference/` are listed in `.gitignore`; they hold several
gigabytes of downloads and must stay out of version control.

## Development environment

Verified on 2026-10-26: macOS 26.7.1, Intel x86_64 (i9-9880H).

Two ESP-IDF installations live side by side under `bin/`, because the two
reference projects require different SDK lines. Only the 5.5.3 one is needed to
build this firmware; the 6.1 one exists because the XiaoZhi reference project
does not support 5.x.

| Purpose | ESP-IDF | Activation | Tools path |
| --- | --- | --- | --- |
| This firmware | v5.5.3 | `source scripts/idf-env.sh` | `bin/espressif` |
| XiaoZhi reference | v6.1 | `source scripts/idf6-env.sh` | `bin/espressif-6` |

Never activate both in one shell.

```text
bin/esp-idf-v5.5.3   ESP-IDF 5.5.3 source, all submodules
bin/espressif       IDF_TOOLS_PATH: riscv32-esp-elf-gcc 14.2.0, openocd,
                    Python venv (idf5.5_py3.9_env), download and component caches
bin/esp-idf-v6.1    ESP-IDF 6.1 source
bin/espressif-6     IDF_TOOLS_PATH: riscv32-esp-elf-gcc 15.2.0, esptool 5.6,
                    Python venv (idf6.1_py3.12_env)
bin/python          Standalone CPython 3.12.15
```

The standalone Python exists because ESP-IDF 6.x requires Python 3.10 or newer
and the system interpreter here is 3.9. Only `scripts/idf6-env.sh` puts it on
`PATH`.

Everything is reproducible without system-level installs: no `sudo`, no
Homebrew packages, no writes outside this workspace. Reinstallation, after
deleting a `bin/` subtree:

```bash
# ESP-IDF 5.5.3 for this firmware
./scripts/setup-idf.sh esp32c3

# ESP-IDF 6.1 for the XiaoZhi reference project
IDF_TAG=v6.1 IDF_DIR="$PWD/bin/esp-idf-v6.1" \
  IDF_TOOLS_PATH="$PWD/bin/espressif-6" ./scripts/setup-idf.sh esp32c3
```

`setup-idf.sh` repairs an interrupted submodule fetch instead of re-cloning, and
prints `git describe --tags` so the checkout identity is visible.

## Build and validate

```bash
source scripts/idf-env.sh

./tools/validate.sh --firmware   # build, merged 0x0 image, layout checks
./tools/validate.sh --static     # repository checks and host tests
./tools/validate.sh              # complete gate
```

On macOS the static gate cannot pass natively: two demo runtime tests link with
`-Wl,--gc-sections`, which Apple's `ld` rejects. Use the Linux wrapper instead
of editing the gate:

```bash
./scripts/static-gate-linux.sh
```

A successful build is not hardware validation. Flashing is a separate,
explicitly approved step; report `Build`, `Host tests`, `Device tests` and
`Unverified` separately.

## Documentation

| Document | Contents |
| --- | --- |
| [Product specification](docs/product/product-spec.md) | Screens, controls, behaviour rules, acceptance |
| [Upstream documentation index](docs/README.md) | Hardware, engineering and contribution guides |
| [AGENTS.md](AGENTS.md) | Rules for AI-assisted work in this repository |

Hardware facts come from `components/bsp/include/bsp_pins.h` and the
[hardware guide](docs/hardware-design/AI_HARDWARE_DEVELOPMENT_GUIDE.md); the
device is an ESP32-C3 with 8 MB flash, no PSRAM, a 240×320 display, an ES8311
codec and three buttons on one ADC ladder.
