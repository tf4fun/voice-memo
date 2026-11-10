// HTTP client for server discovery, paginated metadata and transcript windows.
//
// 缓存一页元数据（最多 4 条）与一个 2 KiB 正文窗口；音频始终流式读取。
#pragma once

#include "esp_err.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MEMO_API_MAX_ITEMS 4

typedef struct {
    char id[37];
    int64_t recorded_at;
    uint32_t duration_ms;
    char status[16];
    char snippet[80];
} memo_api_item_t;

/* Initialise cache ownership before starting input/network tasks. */
esp_err_t memo_api_init(void);

/* 发现服务端后先探测一次：/ping 确认可用，/server 取限值。
   成功返回 ESP_OK；失败表示这条 mDNS 记录不可用（陈旧记录）。 */
esp_err_t memo_api_probe(void);

/* 服务端声明的单条最长录音（毫秒）。未探测到时为 0，表示不做限制。 */
uint32_t memo_api_max_memo_ms(void);
uint32_t memo_api_max_bytes(void);

/* 拉一次列表，成功后缓存可读。需要网络与服务端地址可用。 */
esp_err_t memo_api_refresh(void);

/* Coalesced event/reconnect refresh request, consumed only by uploader. */
void memo_api_request_refresh(void);
bool memo_api_take_refresh_request(void);

/* 列表是否刚更新过（取走即清零），界面据此决定要不要重绘。 */
bool memo_api_take_updated(void);

/* 是否所有条目都已到终态（ready/failed）。没有条目时返回 true。 */
bool memo_api_all_terminal(void);

int memo_api_count(void);
bool memo_api_item_copy(int index, memo_api_item_t *out);

#define MEMO_API_CURSOR_BYTES 96
typedef struct {
    char next_cursor[MEMO_API_CURSOR_BYTES], previous_cursor[MEMO_API_CURSOR_BYTES];
    int count;
    uint32_t generation; // Successful explicit page moves, not background refreshes.
    int navigation;      // 1 older, -1 newer, 2 latest.
    bool loading;
    esp_err_t error;
} memo_api_page_t;

/* Queued UI requests; only the uploader fetches/replaces the page. Failed moves
 * preserve the displayed page and cursors. direction=0 returns to latest. */
esp_err_t memo_api_request_page(int direction);
bool memo_api_page_copy(memo_api_item_t *items, size_t capacity, memo_api_page_t *page);

#define MEMO_API_TEXT_BYTES 2048
typedef struct {
    char id[37], status[16], revision[65], text[MEMO_API_TEXT_BYTES + 1];
    uint32_t offset, end, total, generation;
    bool pending;
    esp_err_t error;
} memo_api_text_t;

/* Offset/direction describe a byte boundary/window, not a seek into audio.
 * Empty revision starts a fresh read. Cancel invalidates late responses.
 * poll_text is worker-only; request/copy/cancel are input-owner calls. */
esp_err_t memo_api_request_text(const char *id, uint32_t offset, bool previous, const char *revision);
void memo_api_cancel_text(void);
void memo_api_poll_text(void);
bool memo_api_text_copy(memo_api_text_t *out);
