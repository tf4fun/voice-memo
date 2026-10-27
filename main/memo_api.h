// main/memo_api.h —— 服务端 HTTP 客户端：拉列表、拉转写、下载音频。
//
// 只缓存元数据（最多 24 条，约 4 KB），转写与音频按需取，避免常驻内存。
#pragma once

#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

#define MEMO_API_MAX_ITEMS 24

typedef struct {
    char id[37];
    int64_t recorded_at;
    uint32_t duration_ms;
    char status[10];
    char snippet[80];
} memo_api_item_t;

/* 发现服务端后先探测一次：/ping 确认可用，/server 取限值。
   成功返回 ESP_OK；失败表示这条 mDNS 记录不可用（陈旧记录）。 */
esp_err_t memo_api_probe(void);

/* 服务端声明的单条最长录音（毫秒）。未探测到时为 0，表示不做限制。 */
uint32_t memo_api_max_memo_ms(void);

/* 拉一次列表，成功后缓存可读。需要网络与服务端地址可用。 */
esp_err_t memo_api_refresh(void);

/* 列表是否刚更新过（取走即清零），界面据此决定要不要重绘。 */
bool memo_api_take_updated(void);

/* 由推送事件更新单条缓存（新增或覆盖）。 */
void memo_api_upsert(const char *id, int64_t recorded_at, uint32_t duration_ms,
                     const char *status, const char *snippet);

/* 是否所有条目都已到终态（ready/failed）。没有条目时返回 true。 */
bool memo_api_all_terminal(void);

int memo_api_count(void);
const memo_api_item_t *memo_api_item(int index);

/* 取一条的转写全文；无转写时返回 ESP_ERR_NOT_FOUND。 */
esp_err_t memo_api_fetch_transcript(const char *id, char *out, size_t cap);

/* 把原始录音下载到 path（覆盖写）。 */
esp_err_t memo_api_download_audio(const char *id, const char *path);
