<p align="right">
  <a href="README.md">English</a> · <strong>简体中文</strong>
</p>

# 语音日记 — 基于 FoloToy AI Passport

给 [FoloToy AI Passport](https://github.com/FoloToy/ai-passport) 穿戴设备做的语音日记固件：
按住一个键说几句，服务器把录音转成文字，之后翻回来当文字读，按一下就能听原声。

本仓库是 `FoloToy/ai-passport` 的派生仓库，保留了上游历史与 remote，方便继续合并基线更新。
应用开发在 `feature/voice-memo` 分支进行，`main` 保持为干净的上游基线。

当前状态：录音、播放、服务器转写、浏览和联网模块已实现，应用尚未完整验收。
交付状态及所有待完善工作统一在[里程碑](docs/project/milestones.zh_CN.md)跟踪；
[产品方案](docs/product/product-spec.zh_CN.md)描述当前行为。

## 仓库构成

| 路径 | 纳入版本控制 | 用途 |
| --- | --- | --- |
| `main/` | 是 | 应用：页面、状态机、任务 |
| `components/bsp/` | 是 | 板级支持：显示、音频、按键、电量 |
| `server/` | 是 | Go API、SQLite、转写任务、mDNS、SSE |
| `docs/product/`、`docs/technical/`、`docs/design/` | 是 | 当前产品行为及实现 |
| `docs/project/` | 是 | 里程碑、待完善项及验收 |
| `scripts/` | 是 | 可复用的开发与校验脚本 |
| `tools/` | 是 | 上游仓库门禁与固件校验 |
| `bin/` | **否** | 本地 ESP-IDF、工具链与 Python 运行时 |
| `.agents/skills/passport-*` | **否** | 指向 `skills/` 下五个必需技能的仓库本地链接 |
| `.dsh/` | **否** | 旧的本地 AI 工具配置 |
| `reference/` | **否** | 参考项目检出 |

`bin/`、`.dsh/`、`reference/` 已写入 `.gitignore`：它们有几个 GB 的下载内容，不进版本库。

## 开发环境

验证环境（2026-10-26）：macOS 26.7.1，Intel x86_64（i9-9880H）。

`bin/` 下并排放着两套 ESP-IDF，因为两个参考项目要求的 SDK 大版本不同。构建本固件只用
5.5.3 那一套；6.1 那套是因为 XiaoZhi 参考项目已不支持 5.x。

| 用途 | ESP-IDF | 激活 | 工具路径 |
| --- | --- | --- | --- |
| 本固件 | v5.5.3 | `source scripts/idf-env.sh` | `bin/espressif` |
| XiaoZhi 参考项目 | v6.1 | `source scripts/idf6-env.sh` | `bin/espressif-6` |

不要在同一个 shell 里同时激活两套。

```text
bin/esp-idf-v5.5.3   ESP-IDF 5.5.3 源码（含全部子模块）
bin/espressif       IDF_TOOLS_PATH：riscv32-esp-elf-gcc 14.2.0、openocd、
                    Python venv（idf5.5_py3.9_env）、下载与组件缓存
bin/esp-idf-v6.1    ESP-IDF 6.1 源码
bin/espressif-6     IDF_TOOLS_PATH：riscv32-esp-elf-gcc 15.2.0、esptool 5.6、
                    Python venv（idf6.1_py3.12_env）
bin/python          standalone CPython 3.12.15
```

standalone Python 是因为 ESP-IDF 6.x 要求 Python ≥ 3.10，而本机系统解释器是 3.9。
只有 `scripts/idf6-env.sh` 会把它放进 `PATH`。

整套环境不需要任何系统级安装即可复现：没有 `sudo`、没有 Homebrew 包、不往工作区外写文件。
删掉 `bin/` 某个子树后可以这样重装：

```bash
# 本固件用的 ESP-IDF 5.5.3
./scripts/setup-idf.sh esp32c3

# XiaoZhi 参考项目用的 ESP-IDF 6.1
IDF_TAG=v6.1 IDF_DIR="$PWD/bin/esp-idf-v6.1" \
  IDF_TOOLS_PATH="$PWD/bin/espressif-6" ./scripts/setup-idf.sh esp32c3
```

`setup-idf.sh` 对中断的子模块拉取是"修复"而不是重新克隆，并会打印 `git describe --tags`，
让检出身份可见。

### Go 工具链（服务端）

服务端需要 Go 1.27.3，本机已全局安装。它的模块缓存与构建缓存默认落在 `~/go` 和
`~/Library/Caches/go-build`；脚本把它们移到被忽略的 `bin/go` 下，工作区保持自包含，
也不碰沙箱之外的目录。

```bash
source scripts/go-env.sh
(cd server && go build ./... && go test ./...)
./scripts/e2e-server.sh   # 真实进程 + 假 ASR；当前音频断言仍要求 WAV
```

## 构建与校验

```bash
source scripts/idf-env.sh

./tools/validate.sh --firmware   # 构建、合并 0x0 镜像、分区布局校验
./tools/validate.sh --static     # 仓库检查与宿主测试
./tools/validate.sh              # 完整门禁
```

macOS 上静态门禁无法原生通过：demo 运行时测试用 `-Wl,--gc-sections` 链接，Apple 的
`ld` 不支持。请用 Linux 包装脚本，而不是改门禁本身：

```bash
./scripts/static-gate-linux.sh
```

构建成功不等于硬件验证。烧录是独立且需要明确授权的步骤；交付时分列
`Build`、`Host tests`、`Device tests`、`Unverified`。

## 文档

| 文档 | 内容 |
| --- | --- |
| [产品方案](docs/product/product-spec.zh_CN.md) | 当前界面、操作与行为 |
| [设备端方案](docs/technical/client.zh_CN.md) | 固件模块、音频、存储与联网 |
| [服务端方案](docs/technical/server.zh_CN.md) | 存储、ASR、HTTP API 与配置 |
| [视觉设计语言](docs/design/visual-language.zh_CN.md) | 配色、卡片几何、页面构成、动效 |
| [项目里程碑](docs/project/milestones.zh_CN.md) | 交付状态、全部待完善项、历史证据与验收 |
| [上游文档索引](docs/README.zh_CN.md) | 硬件、工程与贡献规范 |
| [AGENTS.md](AGENTS.zh_CN.md) | 本仓库 AI 协作规则 |

硬件事实以 `components/bsp/include/bsp_pins.h` 与
[硬件开发指南](docs/hardware-design/AI_HARDWARE_DEVELOPMENT_GUIDE.zh_CN.md) 为准；设备是
ESP32-C3、8 MB Flash、无 PSRAM、240×320 屏、ES8311 codec、三个按键共用一路 ADC 分压。
