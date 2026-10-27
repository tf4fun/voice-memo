<p align="right">
  <a href="server.md">English</a> · <strong>简体中文</strong>
</p>

# 语音日记服务端 — 技术方案

实现快照：2026-10-27。本文描述当前 Go 服务端、存储和 HTTP 协议。
固件见[设备端方案](client.zh_CN.md)，工作和验收只在
[里程碑](../project/milestones.zh_CN.md)中跟踪。

## 1. 范围与运行时

单个 Go 进程接收录音、保存原始 Opus 容器、后台转写，向局域网客户端提供元数据和音频。
使用 Go 1.27.3、`net/http`、`modernc.org/sqlite` SQLite、`pion/opus` 解码及
`grandcat/zeroconf` mDNS。无需外部数据库、消息代理、ffmpeg 或 cgo。
没有网页 UI、认证或 TLS 监听器。

## 2. 包结构

| `server/` 下路径 | 职责 |
| --- | --- |
| `cmd/memo-server` | `serve`、`gc`、`version`，配置及进程生命周期 |
| `cmd/memogen` | 从 Ogg Opus 生成 MEMO 测试文件 |
| `internal/config` | 环境变量解析与校验 |
| `internal/domain` | 记录/状态类型、容器头和帧读取器 |
| `internal/audio` | 结构校验、Opus 解码及 WAV 输出 |
| `internal/store` | SQLite 表结构、查询及任务持久化 |
| `internal/httpapi` | HTTP 路由、JSON、上传、音频、SSE |
| `internal/asr` | 转写接口、SiliconFlow 和假实现 |
| `internal/jobs` | 一个转写执行循环 |
| `internal/events` | 内存事件广播中心 |
| `internal/discovery` | 局域网服务广播 |

## 3. 存储与生命周期

SQLite 使用 WAL，表结构版本为 1。`devices` 保存来源 ID 及首次/最近出现时间；
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
同设备重传返回已有结果，另一设备收到 409。临时文件使用唯一名称避免冲突，
接收成功前同步临时文件和目标目录。
请求体流式写入临时文件并限制字节数，普通失败通过延迟清理移除临时文件。
到达 `keepRejected` 的结构校验失败会把容器保留在 `rejected/`，不是所有拒绝都会保留。
启动没有遗留临时文件或孤立文件清扫。

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

提供商 400/401/403/413/415 及本地解码失败为永久错误，其余 HTTP 错误（包括提供商 429）
可重试。一个执行循环每 2 秒轮询并串行处理到期任务，最多尝试三次：
首次临时失败延迟 30 秒，第二次延迟 2 分钟，第三次永久失败。
退避数组虽含 10 分钟，但三次尝试策略不会调度到它。
`MEMO_ASR_CONCURRENCY` 会解析，但不创建更多执行循环。

状态为 `uploaded → transcribing → ready`，或重试回 `uploaded`，或最终 `failed`。
启动在执行任务前调用 `RecoverJobs` 恢复中断转写。
初始记录与任务入队在同一事务中；入队失败回滚记录，处理函数移除本次新发布的音频。
重传不会重复入队。

## 5. 服务发现

mDNS 广播 `_voicememo._tcp`、可配置实例名（默认主机名）及 HTTP 端口。
TXT 包含 `ver=1`、`path=/api/v1`、`caps=opus`，有非回环 IPv4 时附带 `ip=`。
使用找到的第一个此类接口地址，没有按通往设备的路由显式选址。
客户端可在 SRV/A 解析不完整时回退到 TXT。

广播失败只记日志，不停止 HTTP。多播需要网络可见；本地进程或 host 网络容器提供直接
局域网访问。固件没有手动输入服务器地址的入口。

## 6. HTTP 路由

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
| DELETE | `/memos/{id}` | 软删记录/移除任务并尝试删音频；204，未知 ID 404 |
| GET | `/memos/{id}/audio` | 原始 MEMO 字节，`application/octet-stream`，支持 Range |
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
返回 `items` 与 `next_cursor`，游标编码 `recorded_at:id`。
没有时间范围或转写搜索参数。每行包含 `id`、`device_id`、`recorded_at`、
`uploaded_at`、`duration_ms`、`status`、可选 `lang` 及转写前 40 字的
`snippet`（截断时加省略号）。

详情增加非零/非空 `size_bytes`、`codec`、`transcript`、`edited_at`、`error`、
ASR 元数据和音频路径/大小。可选空值省略，不输出 JSON null。
`audio.bytes` 是原始容器大小。

`PATCH` 只接受 `transcript` 和/或 `lang`，拒绝未知字段。删除不能通过 API 撤销：
记录隐藏并立即尝试删除音频；文件删除失败只记日志。音频端点使用 `http.ServeContent`，
Range 的字节偏移指向容器，不是 PCM。固件当前每次都从头播放。

## 7. 错误与事件

错误格式为 `{"error":{"code":"...","message":"..."}}`。

| 错误码 | HTTP 状态 | 含义 |
| --- | --- | --- |
| `not_found` | 404 | 记录未知/已删除或音频缺失 |
| `invalid_json` | 400 | 请求体、未知字段、条数或游标非法 |
| `missing_device` | 400 | 上传缺来源 ID |
| `bad_container` | 400 | 容器/头部/ID 或结构校验错误 |
| `frame_mismatch` | 400 | 包装为 `ErrFrameMismatch` 的错误 |
| `payload_too_large` | 413 | 请求体超过字节上限 |
| `id_conflict` | 409 | 已有 UUID 归属另一设备 |
| `over_limit` | 400 | 头部时长超过配置上限 |
| `internal` | 500 | 存储/查询或其它服务端错误 |

SSE 先发送 `event: ready` 和 `data: {}`，随后发送 `event: memo` JSON，含
`type`、`id`、`status` 及可选 `snippet`、`recorded_at`、`duration_ms`。
每 20 秒发送注释心跳。上传和任务状态变化会发布事件；PATCH、DELETE 及手动重新入队
没有独立管理通知。

广播中心为每个订阅者缓冲八条，满时跳过投递，没有持久化事件日志、重放游标或必达保证，
客户端靠列表读取对账。任务事件的 snippet 携带完整转写，列表摘要则限 40 字。

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
| `memo-server gc` | 校验配置/打开存储并输出占位说明，不执行清扫 |

| 变量 | 默认值 | 当前用途 |
| --- | --- | --- |
| `MEMO_ADDR` | `:8080` | 监听地址 |
| `MEMO_DATA_DIR` | `./data` | 数据库/音频目录 |
| `MEMO_MDNS_NAME` | 主机名 | 广播实例名 |
| `MEMO_ASR_PROVIDER` | `siliconflow` | 仅 `siliconflow` 或 `fake` |
| `MEMO_ASR_API_KEY` | 空 | SiliconFlow 启动时必需 |
| `MEMO_ASR_MODEL` | `FunAudioLLM/SenseVoiceSmall` | multipart 模型 |
| `MEMO_ASR_BASE_URL` | `https://api.siliconflow.cn` | 提供商 URL 前缀 |
| `MEMO_ASR_CONCURRENCY` | `1` | 校验为正，执行循环仍只有一个 |
| `MEMO_MAX_BYTES` | `33554432` | 流式上传字节限制 |
| `MEMO_MAX_MEMO_MS` | `300000` | 声明及按帧计算的时长上限，告知客户端 |
| `MEMO_RETENTION_DAYS` | `0` | 校验非负，没有保留期清扫 |
| `MEMO_LOG_LEVEL` | `info` | debug/info/warn/error |

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

`scripts/e2e-server.sh` 使用真实进程和假 ASR，当前音频断言仍要求 RIFF/WAV，
但端点已返回 MEMO，因此该脚本不能证明当前协议端到端通过。
实际结果及后续检查记录在[里程碑](../project/milestones.zh_CN.md)。
