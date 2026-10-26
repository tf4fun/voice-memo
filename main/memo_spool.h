// main/memo_spool.h —— 本地录音暂存区（SPIFFS）。
//
// 文件名就是全部索引：未同步 <id>.memo，已上传 <id>.sent，被服务端拒绝
// <id>.bad。没有单独的元数据文件，也就不会出现"索引与文件不一致"。
#pragma once

#include "esp_err.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MEMO_SPOOL_MOUNT_POINT "/spool"
#define MEMO_SPOOL_MAX_ENTRIES 16

typedef enum {
    MEMO_SPOOL_PENDING = 0, /* .memo */
    MEMO_SPOOL_SENT,        /* .sent */
    MEMO_SPOOL_BAD,         /* .bad */
} memo_spool_state_t;

typedef struct {
    char id[37];
    memo_spool_state_t state;
    uint32_t size;
} memo_spool_entry_t;

/* 挂载分区（首次挂载自动格式化）。幂等。 */
esp_err_t memo_spool_mount(void);

/* 新建一个待录文件，写入 80 字节占位头，返回文件句柄。 */
esp_err_t memo_spool_create(const char *id, const uint8_t header[80], void **file_out);

/* 追加一批编码后的帧（长度前缀已在调用方拼好）。 */
esp_err_t memo_spool_append(void *file, const void *data, size_t len);

/* 回填头部帧数与时长并关闭文件。size_out 可为 NULL。 */
esp_err_t memo_spool_finalize(void *file, uint32_t frame_count, uint32_t duration_ms,
                              uint32_t *size_out);

/* 放弃这次录音：关闭并删除文件。 */
esp_err_t memo_spool_discard(void *file, const char *id);

/* 扫描目录，返回条目数；out 可为 NULL 只做计数。 */
int memo_spool_scan(memo_spool_entry_t *out, size_t max_entries);

/* 改变状态（重命名）。上传成功后调用。 */
esp_err_t memo_spool_set_state(const char *id, memo_spool_state_t state);

/* 删除某一状态的遗留文件。 */
esp_err_t memo_spool_remove(const char *id, memo_spool_state_t state);

/* 剩余空间（字节），失败返回 0。 */
size_t memo_spool_free_bytes(void);
