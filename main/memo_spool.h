// main/memo_spool.h —— 本地录音暂存区（SPIFFS）。
//
// 文件名就是全部索引：未同步 <id>.memo，已上传 <id>.sent，被服务端拒绝
// <id>.bad，旧版保留 <id>.keep（只兼容读取）。没有单独的元数据文件。
#pragma once

#include "esp_err.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifndef MEMO_SPOOL_MOUNT_POINT
#define MEMO_SPOOL_MOUNT_POINT "/spool"
#endif
#define MEMO_SPOOL_MAX_ENTRIES 16

typedef enum {
    MEMO_SPOOL_PENDING = 0, /* .memo */
    MEMO_SPOOL_SENT,        /* .sent */
    MEMO_SPOOL_BAD,         /* .bad */
    MEMO_SPOOL_LEGACY_KEPT,        /* .keep: legacy input only; no new retained archive */
    MEMO_SPOOL_RECORDING,         /* .rec: never eligible for upload */
    MEMO_SPOOL_DRAFT,             /* .draft: durable, awaiting confirmation or autosave undo expiry */
} memo_spool_state_t;

typedef struct {
    char id[37];
    memo_spool_state_t state;
    uint32_t size;
    uint64_t recorded_at; /* 取自容器头；读不到时为 0 */
    uint32_t duration_ms;
    uint32_t frame_count;
} memo_spool_entry_t;

/* Mount without erasing existing data. Only a fully erased partition may
 * be formatted automatically on first use. Mount failure blocks capture/upload. */
esp_err_t memo_spool_mount(void);

/* 新建一个待录文件，写入 80 字节占位头，返回文件句柄。 */
esp_err_t memo_spool_create(const char *id, const uint8_t header[80], void **file_out);

/* 追加一批编码后的帧（长度前缀已在调用方拼好）。 */
esp_err_t memo_spool_append(void *file, const void *data, size_t len);

/* 回填头部帧数与时长并关闭文件。size_out 可为 NULL。 */
esp_err_t memo_spool_finalize(void *file, const char *id, uint32_t frame_count, uint32_t duration_ms,
                              uint32_t *size_out);

/* 放弃这次录音：关闭并删除文件。 */
esp_err_t memo_spool_discard(void *file, const char *id);

/* Return the total recognised count; copy at most max_entries, prioritising
 * capture/draft, pending, rejected/legacy, then acknowledged compatibility files.
 * Stable within each priority. No file is changed or deleted by scanning.
 * out may be NULL for count only. */
int memo_spool_scan(memo_spool_entry_t *out, size_t max_entries);

/* 改变状态（重命名）。上传成功后调用。 */
esp_err_t memo_spool_set_state(const char *id, memo_spool_state_t state);

/* Rename a known state without deleting the recording. */
esp_err_t memo_spool_move(const char *id, memo_spool_state_t from, memo_spool_state_t to);

/* 删除某一状态的遗留文件。 */
esp_err_t memo_spool_remove(const char *id, memo_spool_state_t state);

/* Compatibility alias for recovery; does not delete empty recordings. */
int memo_spool_drop_empty(void);

/* Recover complete frames after interruption into a draft, never auto-upload
 * or silently discard damaged recordings. Returns recovered/quarantined count. */
int memo_spool_recover(void);

/* Failed recovery I/O holds capture/upload until recovery succeeds. */
bool memo_spool_recovery_blocked(void);

/* 剩余空间（字节），失败返回 0。 */
size_t memo_spool_free_bytes(void);
