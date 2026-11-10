<p align="right">
  <a href="server.md">English</a> · <strong>简体中文</strong>
</p>

# 语音日记服务端 — 技术方案

实现快照：2026-11-10。本文描述当前 Go 服务端、存储和 HTTP 协议。
固件见[设备端方案](client.zh_CN.md)，工作和验收只在
[里程碑](../project/milestones.zh_CN.md)中跟踪。

## 1. 范围与运行时

单个 Go 进程接收录音、保存原始 Opus 容器、后台转写，向局域网客户端提供元数据和音频。
使用 Go 1.27.3、`net/http`、`modernc.org/sqlite` SQLite、`pion/opus` 解码及
`grandcat/zeroconf` mDNS。无需外部数据库、消息代理、ffmpeg 或 cgo。
内嵌简易网页管理界面，HTTP、HTML 模板与资源嵌入仅使用 Go 标准库，配合原生 CSS/JavaScript，
无需前端依赖包或资源构建。没有认证或 TLS 监听器。

## 2. 包结构

| `server/` 下路径 | 职责 |
| --- | --- |
| `cmd/memo-server` | `serve`、`gc`、`version`，配置及进程生命周期 |
| `cmd/memogen` | 从 Ogg Opus 生成 MEMO 测试文件 |
| `internal/config` | 环境变量解析与校验 |
| `internal/domain` | 记录/状态类型、容器头和帧读取器 |
| `internal/audio` | 结构校验、Opus 解码及 WAV 输出 |
| `internal/store` | SQLite 表结构、查询及任务持久化 |
| `internal/httpapi` | HTTP 路由、内嵌网页管理、JSON、上传、音频、SSE |
| `internal/asr` | 转写接口、SiliconFlow 和假实现 |
| `internal/jobs` | 有界转写任务池与确认退出 |
| `internal/events` | 内存事件广播中心 |
| `internal/discovery` | 局域网服务广播 |

## 3. 存储与生命周期

SQLite 使用 WAL、`synchronous=FULL`，schema 版本为 1。`devices` 保存来源 ID 及首次/最近出现时间；
`memos` 保存录音/上传时间、时长/帧数/字节数、编码、状态、转写/语言、ASR 身份/追踪、
编辑/错误字段、音频路径、时钟偏差与软删除时间；`jobs` 保存唯一记录 ID、尝试次数及到期时间。
列表索引按 `recorded_at DESC, id DESC` 排列未删除记录。

文件位于 `MEMO_DATA_DIR` 下：

```text
memo.db
incoming/<UUID>-<random>.part
audio/<device_id>/<YYYYMM>/<UUID>.memo
rejected/<UUID>.memo
```

原始容器是权威音频。上传只做结构校验，不解码 Opus；发布音频时不覆盖已有文件，
元数据与初始任务在同一 SQLite 事务提交。同 ID 请求用固定数量的锁串行处理，
同设备重传返回已有结果，另一设备收到 409；已明确删除的 UUID 重传返回 410，
不重新创建音频或任务。临时文件使用唯一名称避免冲突，
接收成功前同步临时文件和从目标目录到数据根目录的整条目录链。
请求体流式写入临时文件并限制字节数，普通失败通过延迟清理移除临时文件。
到达 `keepRejected` 的结构校验失败会把容器保留在 `rejected/`，不是所有拒绝都会保留。
没有自动清扫。显式 `gc` 预览旧临时/孤立文件，`gc --apply` 才删除符合条件的文件，
不删除录音记录或被拒文件。

SiliconFlow 适配器仅在转写请求时把容器解为内存 WAV，不保存为权威音频文件。
因此已经接收、但 Opus 载荷无法解码的容器仍可能在转写阶段失败。
假提供商直接返回固定文字，不解码。

`recorded_at` 来自容器。为零或与接收时间相差超过 600 秒时，使用服务器时间并在
`clock_skew` 记录差值。列表排序和月份路径使用修正后的时间。

## 4. 转写

`asr.Transcriber` 暴露 `Name()` 和
`Transcribe(context.Context, asr.Input) (asr.Result, error)`。
输入包含存储的容器路径、时长和语言；输出包含文字、语言、提供商、模型及追踪 ID。
`ErrPermanent` 区分不可重试错误。

SiliconFlow 使用 bearer key 向 `<MEMO_ASR_BASE_URL>/v1/audio/transcriptions`
发送 multipart `file`（解码 WAV）和 `model`，读取 `text` 与
`x-siliconcloud-trace-id`。适配器执行自身的 50 MiB/一小时限制，默认 HTTP 超时 120 秒。
这些是代码常量，不代表提供商最新服务政策。

提供商 400/401/403/413/415 及本地解码失败为永久错误，其余 HTTP 错误（含提供商 429）可重试。配置的 1–4 个任务独立领取任务，每 2 秒轮询。
每次尝试有 90 秒上下文时限，短于 120 秒领取租约；容器读取和 HTTP 均响应取消。
最多三次尝试，暂时性失败分别延迟 30 秒、2 分钟。

领取身份含任务行 ID 和尝试次数。状态、结果和队列变更只有在同一领取身份仍持有录音时
才一起提交。人工编辑设为 ready 并移除任务，删除移除任务，重新转写以事务新建自增任务 ID。
旧结果或旧租约尝试不能覆盖这些变化，也不能删除替代任务。退出时取消并等待任务池全部结束，
再关闭 SQLite；中断任务保留供恢复。

状态流为 `uploaded → transcribing → ready`、重试回 `uploaded` 或终止 `failed`。
启动恢复中断任务；初次录音/任务接收在一个事务内，重传不重复入队。

## 5. 服务发现

mDNS 在一个已启用的多播 IPv4 局域网接口上广播 `_voicememo._tcp`。实例名可配置，默认主机名，
去除尾部 `.local`，将点号/其它标点规范为单个最多 63 字节的 UTF-8 标签。使用显式 ASCII
`.local.` 主机名和该接口的 IPv4 注册，避免内置注册器在 macOS 上生成 `.local.local` 目标。
TXT 包含 `ver=1`、`path=/api/v1`、`caps=opus`，`ip=` 与 A 记录使用同一个地址。
客户端可在 SRV/A 解析不完整时回退到 TXT。

选择时排除未启用、回环、不支持多播和点对点接口，以及不可用/链路本地 IPv4。默认使用首个
合格接口，`MEMO_MDNS_INTERFACE` 可明确选择。指定接口不可用时报告广播失败，不悄悄切到其它
网络。接口/地址变化后需重启。仅监听回环地址的 HTTP 服务不广播，防止本机测试覆盖真实局域网
服务记录。

广播失败只记日志，不停止 HTTP。多播需要网络可见；本地进程或 host 网络容器提供直接
局域网访问。固件没有手动输入服务器地址的入口。

## 6. HTTP 路由

同一监听端口在 `/` 提供中文管理页，每页二十条录音，支持设备筛选、游标双向翻页和空白/错误
状态。`/memos/{id}` 展示转录、录音信息和音频播放器。原生 JavaScript 复用现有
PATCH/DELETE/重新转写接口；破坏性操作先通过页面弹窗确认，编辑失败保留草稿，离开尚未保存
的文字会触发浏览器提示。页面可见且未编辑时，每四秒更新待处理转写状态，过期轮询回复不能
覆盖较新的人工修改。列表手动刷新。

模板对录音文字转义，CSS/JavaScript 内嵌在 `/assets/app.css` 与 `/assets/app.js`。
HTML 响应不缓存，并使用同源内容策略。标准库跨源保护拒绝其它来源的浏览器修改请求，
没有浏览器来源头的设备请求仍可使用。这是请求保护，不是用户认证。

浏览器播放/下载使用 `/api/v1/memos/{id}/audio.wav`，复用现有 Opus 解码器写入私密临时 WAV，
请求结束删除，不新增 RAM 或持久化 PCM 缓存。每个进程只允许一条转换及传输并发，
另一播放请求返回 503 与 `Retry-After: 2`。转换检查取消并按帧推导大小限制 PCM 输出，保留
容器声明时长的容差。WAV 支持 HEAD/Range 且不缓存；原 `/audio` 仍向设备提供 MEMO 字节。

下表路径统一带 `/api/v1` 前缀。除上传、音频和 SSE 外使用 JSON。
读取可访问全部设备，列表可选设备过滤。

| 方法 | 路径 | 当前响应/行为 |
| --- | --- | --- |
| GET | `/health` | 服务/版本、表版本、数据库状态、时间、运行时长；数据库 ping 失败为 503 |
| GET | `/ping` | 200，`{"service":"memo","api":1}` |
| GET | `/server` | 版本、API、时钟、提供商/模型、限值、队列深度 |
| POST | `/memos` | 上传，新建 201，已有 UUID 200 |
| GET | `/memos` | 游标列表，最新在前 |
| GET | `/memos/{id}` | 详情、转写、ASR 和音频元数据 |
| PATCH | `/memos/{id}` | 修改转写和/或 `lang`，记录编辑时间，设为 `ready` |
| DELETE | `/memos/{id}` | 事务内隐藏记录/移除任务，再删除音频；204（含已知 ID 重试），未知 ID 404，文件删除失败 500 |
| GET | `/memos/{id}/audio` | 原始 MEMO 字节，`application/octet-stream`，支持 Range |
| GET | `/memos/{id}/audio.wav` | 按需生成浏览器 WAV 播放/下载，支持 HEAD/Range |
| GET | `/memos/{id}/text` | 有界 UTF-8 显示窗口、字节偏移及内容版本 |
| POST | `/memos/{id}/transcribe` | 设为 `uploaded`、清错误并入队；202 |
| GET | `/events` | `text/event-stream` 订阅 |

### 上传

```http
POST /api/v1/memos
Content-Type: application/octet-stream
X-Device-Id: A1B2C3D4E5F6
X-Memo-Id: 00000000-0000-4000-8000-000000000000

<MEMO container bytes>
```

`X-Device-Id` 去除两端空白后必须匹配 `[A-Za-z0-9_-]{1,64}`；`X-Memo-Id` 必须符合带连字符 UUID 语法，并与容器一致。
录音时间来自容器，不读取额外的 `X-Recorded-At` 头。
已知、未删除的 UUID 返回原结果，不再创建任务。默认上限 32 MiB、300000 ms。

```json
{"id":"00000000-0000-4000-8000-000000000000","status":"uploaded","duration_ms":1000}
```

### 列表与详情

`GET /memos?limit=20&cursor=<opaque>&device=<optional>` 默认 20 条，上限 100，
返回 `items`、`next_cursor` 和 `previous_cursor`，游标编码 `recorded_at:id`。
默认 `direction=older` 继续读取锚点之后的旧记录；`direction=newer` 必须提供游标，选择最近的
较新一页。两者均按最新在前返回并保持设备过滤。页面行与游标存在性检查使用同一 SQLite 事务。
恰好填满的末页不再提供虚假的下一页；因删除而变空的页面仍提供返回方向的游标。
没有时间范围或转写搜索参数。每行包含 `id`、`device_id`、`recorded_at`、
`uploaded_at`、`duration_ms`、`status`、可选 `lang` 及转写前 40 字的
`snippet`（截断时加省略号）。

详情增加非零/非空 `size_bytes`、`codec`、`transcript`、`edited_at`、`error`、
ASR 元数据和音频路径/大小。可选空值省略，不输出 JSON null。
`audio.bytes` 是原始容器大小。

`GET /memos/{id}/text?offset=0&limit=2048&direction=next&revision=<optional>`
读取显示窗口，不发送整个转写。每窗 4–4096 字节，默认 2048，方向为 `next` 或 `previous`。
偏移按 UTF-8 字节计，必须落在字符边界；向前窗口从偏移开始，向后窗口在该处结束。
响应含 `id`、`status`、`text`、`offset`、`end`、`total` 及 64 字符 SHA-256 `revision`。
下一窗使用 `end`，上一窗使用 `offset`，客户端始终只需保存一个窗口。

显示表示将 NUL 和非法 UTF-8 替换为 U+FFFD，版本哈希基于该表示。提供的版本过旧时返回
409 `transcript_changed`，要求重新读取。空文本仍返回状态和零边界；不存在或已删除条目返回
404。原有完整详情接口未变，旧单向列表客户端可以忽略新增游标字段。

`PATCH` 只接受 `transcript` 和/或 `lang`，拒绝未知字段。删除不能通过 API 撤销：
记录隐藏并立即尝试删除音频。隐藏记录与撤销任务在同一 SQLite 事务中，失败一起回滚。
文件删除失败时记录日志、发布元数据删除事件并返回 500。
对已知且已删除 ID 重复 DELETE 可重试删除文件，不改写最初删除时间；
清理成功或文件已不存在返回 204，未知 ID 仍为 404。音频端点使用 `http.ServeContent`，
Range 的字节偏移指向容器，不是 PCM。固件当前每次都从头播放。

## 7. 错误与事件

错误格式为 `{"error":{"code":"...","message":"..."}}`。

| 错误码 | HTTP 状态 | 含义 |
| --- | --- | --- |
| `not_found` | 404 | 记录未知/已删除或音频缺失 |
| `invalid_json` | 400 | 请求体、未知字段、条数、游标、方向或正文边界非法 |
| `missing_device` | 400 | 上传缺来源 ID |
| `bad_container` | 400 | 容器/头部/ID 或结构校验错误 |
| `frame_mismatch` | 400 | 包装为 `ErrFrameMismatch` 的错误 |
| `payload_too_large` | 413 | 请求体超过字节上限 |
| `id_conflict` | 409 | 已有 UUID 归属另一设备 |
| `transcript_changed` | 409 | 显示文本版本变化，需要重新读取窗口 |
| `deleted` | 410 | 该 UUID 已被明确删除，重传不会重新创建 |
| `over_limit` | 400 | 声明或按帧时长超过上限 |
| `internal` | 500 | 存储/查询或其它服务端错误 |

SSE 先订阅，再发送 `event: ready` / `data: {}`。上传、任务、PATCH、DELETE 和手动
重新转写均发变更事件，删除携带 `type=deleted`。客户端重新读取权威列表，不合并零散事件行。
所有摘要集中限制为 40 字加省略号，即使 JSON 转义也符合固件 512 字节分帧界限。

每 20 秒发送注释心跳，写入时限 5 秒。每个订阅者缓冲八条，满时关闭订阅，强制重连及列表
对账。没有持久化重放或恰好一次保证，重连刷新与周期轮询共同弥补遗漏。

## 8. 上传容器

版本 1，整数字段为小端：

| 偏移 | 大小 | 字段 |
| --- | ---: | --- |
| 0 | 4 | 魔数 `MEMO` |
| 4 | 1 | 版本 1 |
| 5 | 1 | 编码 1，即 Opus |
| 6 | 2 | 采样率，固件写 16000 |
| 8 | 1 | 声道，固件写 1 |
| 9 | 1 | 帧时长 ms，固件写 60 |
| 10 | 2 | 保留 |
| 12 | 4 | 时长 ms |
| 16 | 8 | 录音 UTC 秒，未对时为零 |
| 24 | 36 | 带连字符 ASCII UUID |
| 60 | 4 | 帧数 |
| 64 | 16 | 保留 |
| 80 | 可变 | 重复 uint16 长度及 Opus 包 |

头部解析接受采样率 8000–48000、声道 1 或 2、帧时长 1–60 ms；
实际 Opus 解码还受解码器支持范围约束。上传遍历所有包边界，拒绝空录音、空包、
截断包和任何声明/实际帧数不一致，包括声明零帧。帧数和时长不一致映射为 `frame_mismatch`。

声明时长必须与 `frame_count × frame_ms` 保持 5% 内的误差；声明时长和按帧计算的时长
都不能超过配置上限。上传不解码载荷，无法解码的 Opus 仍可能在后续转写时被拒绝。

## 9. CLI 与配置

| 命令 | 当前行为 |
| --- | --- |
| `memo-server serve` | HTTP、任务执行与广播，默认命令 |
| `memo-server serve -addr host:port` | 覆盖监听地址 |
| `memo-server version` | 输出 `0.1.0` |
| `memo-server gc` | 预览旧的无引用 `.part`/`.memo`，最小年龄 24 小时 |
| `memo-server gc --apply` | 删除合格文件，保留被引用音频、被拒文件及全部数据库行 |

| 变量 | 默认值 | 当前用途 |
| --- | --- | --- |
| `MEMO_ADDR` | `:8080` | 监听地址 |
| `MEMO_DATA_DIR` | `./data` | 数据库/音频目录 |
| `MEMO_MDNS_NAME` | 主机名 | 规范为单标签的广播实例 |
| `MEMO_MDNS_INTERFACE` | 自动 | 一个已启用的多播 IPv4 局域网接口，例如本机的 `en0` |
| `MEMO_ASR_PROVIDER` | `siliconflow` | 仅 `siliconflow` 或 `fake` |
| `MEMO_ASR_API_KEY` | 空 | SiliconFlow 启动时必需 |
| `MEMO_ASR_MODEL` | `FunAudioLLM/SenseVoiceSmall` | multipart 模型 |
| `MEMO_ASR_BASE_URL` | `https://api.siliconflow.cn` | 提供商 URL 前缀 |
| `MEMO_ASR_CONCURRENCY` | `1` | 实际任务数量，范围 1–4 |
| `MEMO_MAX_BYTES` | `33554432` | 流式上传字节限制 |
| `MEMO_MAX_MEMO_MS` | `300000` | 声明及按帧计算的时长上限，告知客户端 |
| `MEMO_RETENTION_DAYS` | `0` | 仅支持零，非零启动失败，无自动过期删除 |
| `MEMO_LOG_LEVEL` | `info` | debug/info/warn/error |

`gc` 只需 `MEMO_DATA_DIR`，不要求 ASR 凭据。`--older-than` 默认 24 小时并拒绝更短时间。
跳过符号链接、新文件、未知类型、所有被引用路径（含软删除行）和 `rejected/`。
执行前复查身份、修改时间和大小。HTTP 上传读取时限 120 秒，短于 GC 年龄下限。
没有自动录音保留期删除。

## 10. 安全与部署

服务面向可信单用户局域网。能访问端口的人可以读取、修改和删除录音；来源 ID 只作归属记录，
不是身份验证。上传校验 UUID 语法及一致性，设备 ID 去除两端空白后限制为
1–64 个 ASCII 字母、数字、下划线或连字符，再用于组成存储路径。

ASR key 来自环境变量，以 bearer 头发送。提供商错误响应片段可能被存储或记日志，
适配器没有对任意提供商文本做脱敏。没有内置 TLS、令牌或账号中间件。

从仓库根目录构建：

```bash
source scripts/go-env.sh
cd server
mkdir -p ../build
CGO_ENABLED=0 go build -trimpath -ldflags="-s -w" -o ../build/memo-server ./cmd/memo-server
MEMO_ASR_PROVIDER=fake ../build/memo-server serve
```

持久化并备份整个数据目录，包括 SQLite 和容器树。运行中的 SQLite 应做一致性数据库备份，
不能在 WAL 仍有写入时只复制 `memo.db`。打开存储时执行迁移。

## 11. 验证入口

`cd server && go test ./...` 覆盖配置、头部/帧解析、测试音频解码、SQLite 操作、
API 流程、提供商请求/错误处理及任务重试。使用临时存储、假转写和本地 HTTP 服务；
首次运行可能需要下载模块/工具链。

网页处理器测试覆盖 HTML 转义、带设备筛选的分页、修改请求来源检查、WAV 播放/Range、转换
边界及临时文件清理。仓库根目录执行 `node --test tests/test_memo_web.js`，仅用 Node 内置模块
验证编辑失败恢复、过期轮询抑制和确认流程；Node 仅用于测试，不是服务端构建或运行依赖。
实际浏览器布局和播放器控件仍需浏览器验收。

`scripts/e2e-server.sh` 使用真实进程、假 ASR 和临时存储，比较原始 MEMO 音频字节及精确 Range
响应，覆盖上传/幂等、转写、双向历史、带版本正文、管理、重新转写和安全 GC，不调用真实提供商。
实际结果及后续检查记录在[里程碑](../project/milestones.zh_CN.md)。


### 本机硅基流动启动

现有适配器向[硅基流动转写端点](https://docs.siliconflow.cn/docs/api/audio-transcriptions-post)
发送解码后的 WAV 与所配置模型。`MEMO_ASR_BASE_URL` 使用根地址（例如
`https://api.siliconflow.cn`），适配器追加 `/v1/audio/transcriptions`。
模型由 `MEMO_ASR_MODEL` 决定，启动器不覆盖用户选择。

本机使用时，仅在没有现有私密配置时从 `server/.env.example` 创建 `server/.env.local`，
填写 API Key，并限制为仅所有者可读写（`chmod 600 server/.env.local`）。仓库根目录执行
`./scripts/run-server.sh`：复用项目 Go 缓存配置，构建 `build/server/memo-server`，
在 `server/` 内运行，因此默认数据目录为 `server/data/`。`MEMO_ENV_FILE` 可指定其它配置文件。
私密文件、默认数据目录、生成二进制/日志均被 Git 忽略；密钥只在服务端使用，不进入设备固件。

配置格式为字面量 `MEMO_NAME=value`，支持空行、独立注释行、CRLF 及可选配对外引号。
不执行 shell 命令、不展开变量、不支持行尾注释。同名配置项覆盖继承的环境变量。
未知名称/畸形行会失败，但不打印值。`./scripts/run-server.sh --check-env` 只检查文件语法，
不验证凭据或调用 API。

进程以前台方式运行，需要保持运行；修改配置后需重启，不安装登录/开机服务。
同局域网设备通过 mDNS 发现已运行服务。默认端口下，本机健康/配置端点为
`http://127.0.0.1:8080/api/v1/health` 及 `/api/v1/server`。提供商模拟测试检查精确 HTTP
路径、multipart 字段和 RIFF/WAVE 载荷；真实提供商测试独立于模拟测试及设备人声验收。
本机启动/解析测试只依赖 Python 和 Bash。
