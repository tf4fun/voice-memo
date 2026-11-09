// main/memo_api.c —— 服务端 HTTP 客户端。
#include "memo_api.h"

#include <stdio.h>
#include <stdatomic.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "memo_text.h"

#include "cJSON.h"
#include "esp_http_client.h"
#include "esp_log.h"

#include "memo_discovery.h"

static const char *TAG = "memo_api";

static memo_api_item_t s_items[MEMO_API_MAX_ITEMS];
static int s_count;
static atomic_bool s_updated;
static SemaphoreHandle_t s_cache_lock;
static memo_api_page_t s_page;
typedef struct { char cursor[MEMO_API_CURSOR_BYTES]; bool newer; int navigation; } page_query_t;
static page_query_t s_query, s_committed_query;
static uint32_t s_page_serial;
static memo_api_text_t s_text;
typedef struct { char id[37], revision[65]; uint32_t offset; bool previous; } text_query_t;
static text_query_t s_text_query;
static uint32_t s_text_serial;

static bool valid_id(const char *id)
{
    if (id == NULL || strlen(id) != 36) return false;
    for (int i = 0; i < 36; i++) {
        const unsigned char c = (unsigned char)id[i];
        if (i == 8 || i == 13 || i == 18 || i == 23) { if (c != '-') return false; }
        else if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return false;
    }
    return true;
}
static bool valid_status(const char *status)
{
    return status != NULL && (strcmp(status, "uploaded") == 0 || strcmp(status, "transcribing") == 0 ||
        strcmp(status, "ready") == 0 || strcmp(status, "failed") == 0);
}
static bool copy_cursor(char out[MEMO_API_CURSOR_BYTES], const cJSON *node)
{
    out[0] = '\0';
    if (node == NULL) return true; // Existing forward-only API-v1 responses.
    if (!cJSON_IsString(node) || strlen(node->valuestring) >= MEMO_API_CURSOR_BYTES) return false;
    for (const char *p = node->valuestring; *p; p++) {
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
              (*p >= '0' && *p <= '9') || *p == '-' || *p == '_')) return false;
    }
    strcpy(out, node->valuestring);
    return true;
}
static bool json_u32(const cJSON *node, uint32_t *out)
{
    if (!cJSON_IsNumber(node) || !(node->valuedouble >= 0 && node->valuedouble <= UINT32_MAX)) return false;
    uint32_t value = (uint32_t)node->valuedouble;
    if (node->valuedouble != value) return false;
    *out = value; return true;
}

esp_err_t memo_api_init(void)
{
    if (s_cache_lock == NULL) s_cache_lock = xSemaphoreCreateMutex();
    return s_cache_lock != NULL ? ESP_OK : ESP_ERR_NO_MEM;
}
static atomic_bool s_refresh_requested;

void memo_api_request_refresh(void) { s_refresh_requested = true; }
bool memo_api_take_refresh_request(void) { return atomic_exchange(&s_refresh_requested, false); }

static atomic_uint_least32_t s_max_memo_ms, s_max_bytes;

/* 响应体缓冲：列表可能几十 KB，按块累积。 */
typedef struct {
    char *data;
    size_t len;
    size_t cap;
    esp_err_t error;
} body_buf_t;

static esp_err_t body_append(body_buf_t *buf, const char *chunk, int chunk_len)
{
    if (buf->error != ESP_OK) return buf->error;
    if (chunk_len < 0 || (size_t)chunk_len > 32768 - buf->len) {
        return buf->error = ESP_ERR_INVALID_SIZE;
    }
    if (buf->len + (size_t)chunk_len + 1 > buf->cap) {
        size_t cap = buf->cap == 0 ? 4096 : buf->cap;
        while (cap < buf->len + (size_t)chunk_len + 1) cap *= 2;
        if (cap > 32769) cap = 32769;
        char *grown = realloc(buf->data, cap);
        if (grown == NULL) return buf->error = ESP_ERR_NO_MEM;
        buf->data = grown;
        buf->cap = cap;
    }
    memcpy(buf->data + buf->len, chunk, (size_t)chunk_len);
    buf->len += (size_t)chunk_len;
    buf->data[buf->len] = '\0';
    return ESP_OK;
}

static esp_err_t http_event(esp_http_client_event_t *evt)
{
    body_buf_t *buf = (body_buf_t *)evt->user_data;
    if (evt->event_id == HTTP_EVENT_ON_DATA && buf != NULL) {
        return body_append(buf, (const char *)evt->data, evt->data_len);
    }
    return ESP_OK;
}

/* 组一个指向服务端的 URL。 */
static esp_err_t build_url(char *out, size_t cap, const char *path, memo_endpoint_t *snapshot)
{
    memo_endpoint_t endpoint;
    if (!memo_discovery_endpoint(&endpoint)) {
        if (memo_discovery_search(3000) != ESP_OK || !memo_discovery_endpoint(&endpoint)) return ESP_ERR_NOT_FOUND;
    }
    if (snapshot != NULL) *snapshot = endpoint;
    int len = snprintf(out, cap, "http://%s:%u%s", endpoint.host, (unsigned)endpoint.port, path);
    return len >= 0 && (size_t)len < cap ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

/* 一次 GET，把响应体读到 buf；成功时返回 HTTP 状态码。 */
static int http_get(const char *path, body_buf_t *buf)
{
    char url[384];
    memo_endpoint_t endpoint;
    if (build_url(url, sizeof(url), path, &endpoint) != ESP_OK) return -1;

    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 3000,
        .buffer_size = 2048,
        .event_handler = http_event,
        .user_data = buf,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == NULL) return -1;

    int code = -1;
    if (esp_http_client_perform(client) == ESP_OK && buf->error == ESP_OK) {
        code = esp_http_client_get_status_code(client);
    }
    esp_http_client_cleanup(client);
    if (code < 0 || code >= 500) memo_discovery_invalidate(&endpoint);
    return code;
}

esp_err_t memo_api_probe(void)
{
    s_max_memo_ms = s_max_bytes = 0;
    memo_endpoint_t endpoint;
    if (!memo_discovery_endpoint(&endpoint)) {
        if (memo_discovery_search(3000) != ESP_OK || !memo_discovery_endpoint(&endpoint)) return ESP_ERR_NOT_FOUND;
    }
    body_buf_t ping = {0};
    const int ping_code = http_get("/api/v1/ping", &ping);
    cJSON *root = ping_code == 200 && ping.data != NULL ? cJSON_Parse(ping.data) : NULL;
    free(ping.data);
    const cJSON *service = cJSON_GetObjectItem(root, "service");
    const cJSON *api = cJSON_GetObjectItem(root, "api");
    bool valid = cJSON_IsString(service) && strcmp(service->valuestring, "memo") == 0 &&
                 cJSON_IsNumber(api) && api->valuedouble == 1;
    cJSON_Delete(root);
    if (!valid) { memo_discovery_invalidate(&endpoint); return ESP_ERR_INVALID_RESPONSE; }
    body_buf_t info = {0};
    const int code = http_get("/api/v1/server", &info);
    root = code == 200 && info.data != NULL ? cJSON_Parse(info.data) : NULL;
    free(info.data);
    api = cJSON_GetObjectItem(root, "api");
    const cJSON *limits = cJSON_GetObjectItem(root, "limits");
    const cJSON *ms = cJSON_GetObjectItem(limits, "max_memo_ms");
    const cJSON *bytes = cJSON_GetObjectItem(limits, "max_bytes");
    valid = cJSON_IsNumber(api) && api->valuedouble == 1 &&
            cJSON_IsNumber(ms) && ms->valuedouble > 0 && ms->valuedouble <= UINT32_MAX &&
            cJSON_IsNumber(bytes) && bytes->valuedouble > 0 && bytes->valuedouble <= UINT32_MAX;
    if (valid) { s_max_memo_ms = (uint32_t)ms->valuedouble; s_max_bytes = (uint32_t)bytes->valuedouble; }
    cJSON_Delete(root);
    if (valid) memo_discovery_confirm(&endpoint);
    else memo_discovery_invalidate(&endpoint);
    return valid ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

uint32_t memo_api_max_bytes(void) { return s_max_bytes; }

uint32_t memo_api_max_memo_ms(void)
{
    return s_max_memo_ms;
}

static esp_err_t page_failure(uint32_t serial, esp_err_t error)
{
    xSemaphoreTake(s_cache_lock, portMAX_DELAY);
    if (serial == s_page_serial) {
        s_page.error = error;
        s_page.loading = false;
        s_query = s_committed_query;
    }
    xSemaphoreGive(s_cache_lock);
    return error;
}

esp_err_t memo_api_request_page(int direction)
{
    if (s_cache_lock == NULL) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_cache_lock, portMAX_DELAY);
    esp_err_t err = ESP_OK;
    const char *cursor = direction > 0 ? s_page.next_cursor : s_page.previous_cursor;
    if (direction != 0 && s_page.loading) err = ESP_ERR_INVALID_STATE;
    else if (direction != 0 && cursor[0] == '\0') err = ESP_ERR_NOT_FOUND;
    else {
        memset(&s_query, 0, sizeof(s_query));
        if (direction != 0) strcpy(s_query.cursor, cursor);
        s_query.newer = direction < 0;
        s_query.navigation = direction > 0 ? 1 : direction < 0 ? -1 : 2;
        s_page_serial++;
        s_page.loading = true; s_page.error = ESP_OK;
    }
    xSemaphoreGive(s_cache_lock);
    if (err == ESP_OK) memo_api_request_refresh();
    return err;
}

bool memo_api_page_copy(memo_api_item_t *items, size_t capacity, memo_api_page_t *page)
{
    if (s_cache_lock == NULL || page == NULL || (capacity && items == NULL)) return false;
    xSemaphoreTake(s_cache_lock, portMAX_DELAY);
    *page = s_page;
    const size_t count = (size_t)s_count < capacity ? (size_t)s_count : capacity;
    if (count) memcpy(items, s_items, count * sizeof(*items));
    xSemaphoreGive(s_cache_lock);
    return true;
}

esp_err_t memo_api_refresh(void)
{
    if (s_cache_lock == NULL) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_cache_lock, portMAX_DELAY);
    const page_query_t query = s_query;
    const uint32_t serial = s_page_serial;
    xSemaphoreGive(s_cache_lock);
    char path[192];
    snprintf(path, sizeof(path), "/api/v1/memos?limit=24&direction=%s&cursor=%s",
             query.newer ? "newer" : "older", query.cursor);
    body_buf_t buf = {0};
    const int code = http_get(path, &buf);
    if (code != 200) {
        ESP_LOGW(TAG, "list failed (http %d)", code);
        free(buf.data);
        return page_failure(serial, ESP_FAIL);
    }

    cJSON *root = cJSON_Parse(buf.data);
    free(buf.data);
    if (root == NULL) {
        ESP_LOGW(TAG, "list is not valid JSON");
        return page_failure(serial, ESP_ERR_INVALID_RESPONSE);
    }

    const cJSON *items = cJSON_GetObjectItem(root, "items");
    if (!cJSON_IsArray(items)) {
        /* 响应结构不对：保留上一份缓存，而不是把列表清空。 */
        ESP_LOGW(TAG, "list response has no items array");
        cJSON_Delete(root);
        return page_failure(serial, ESP_ERR_INVALID_RESPONSE);
    }
    memo_api_page_t page = {0};
    if (!copy_cursor(page.next_cursor, cJSON_GetObjectItem(root, "next_cursor")) ||
        !copy_cursor(page.previous_cursor, cJSON_GetObjectItem(root, "previous_cursor"))) {
        cJSON_Delete(root); return page_failure(serial, ESP_ERR_INVALID_RESPONSE);
    }
    memo_api_item_t *next = calloc(MEMO_API_MAX_ITEMS, sizeof(*next));
    if (next == NULL) { cJSON_Delete(root); return page_failure(serial, ESP_ERR_NO_MEM); }
    int count = 0;
    const cJSON *it = NULL;
    cJSON_ArrayForEach(it, items) {
        if (count >= MEMO_API_MAX_ITEMS) { free(next); cJSON_Delete(root); return page_failure(serial, ESP_ERR_INVALID_RESPONSE); }

        const cJSON *id = cJSON_GetObjectItem(it, "id");
        if (!cJSON_IsString(id) || !valid_id(id->valuestring)) { free(next); cJSON_Delete(root); return page_failure(serial, ESP_ERR_INVALID_RESPONSE); }

        memo_api_item_t *item = &next[count];
        memset(item, 0, sizeof(*item));
        strncpy(item->id, id->valuestring, sizeof(item->id) - 1);

        const cJSON *rec = cJSON_GetObjectItem(it, "recorded_at");
        if (!cJSON_IsNumber(rec) || !(rec->valuedouble >= 0 && rec->valuedouble < 9223372036854775808.0)) {
            free(next); cJSON_Delete(root); return page_failure(serial, ESP_ERR_INVALID_RESPONSE);
        }
        item->recorded_at = (int64_t)rec->valuedouble;

        const cJSON *dur = cJSON_GetObjectItem(it, "duration_ms");
        if (!json_u32(dur, &item->duration_ms)) { free(next); cJSON_Delete(root); return page_failure(serial, ESP_ERR_INVALID_RESPONSE); }

        const cJSON *status = cJSON_GetObjectItem(it, "status");
        if (!cJSON_IsString(status) || !valid_status(status->valuestring)) {
            free(next); cJSON_Delete(root); return page_failure(serial, ESP_ERR_INVALID_RESPONSE);
        }
        strcpy(item->status, status->valuestring);

        const cJSON *snip = cJSON_GetObjectItem(it, "snippet");
        if (cJSON_IsString(snip)) {
            memo_text_prefix(item->snippet, sizeof(item->snippet), snip->valuestring, SIZE_MAX);
        }
        count++;
    }
    cJSON_Delete(root);
    if (s_cache_lock == NULL) { free(next); return ESP_ERR_INVALID_STATE; }
    xSemaphoreTake(s_cache_lock, portMAX_DELAY);
    if (serial != s_page_serial) {
        xSemaphoreGive(s_cache_lock); free(next); return ESP_ERR_INVALID_STATE;
    }
    memcpy(s_items, next, sizeof(s_items));
    s_count = count;
    page.count = count;
    page.generation = s_page.generation + (query.navigation != 0);
    page.navigation = query.navigation != 0 ? query.navigation : s_page.navigation;
    s_page = page;
    s_query.navigation = 0;
    s_committed_query = s_query;
    xSemaphoreGive(s_cache_lock);
    free(next);

    s_updated = true;
    ESP_LOGI(TAG, "list: %d item(s)", s_count);
    return ESP_OK;
}

bool memo_api_take_updated(void) { return atomic_exchange(&s_updated, false); }

bool memo_api_all_terminal(void)
{
    if (s_cache_lock == NULL) return false;
    xSemaphoreTake(s_cache_lock, portMAX_DELAY);
    bool terminal = true;
    for (int i = 0; i < s_count; i++) {
        if (strcmp(s_items[i].status, "ready") != 0 && strcmp(s_items[i].status, "failed") != 0) terminal = false;
    }
    xSemaphoreGive(s_cache_lock);
    return terminal;
}

int memo_api_count(void)
{
    if (s_cache_lock == NULL) return 0;
    xSemaphoreTake(s_cache_lock, portMAX_DELAY);
    int count = s_count;
    xSemaphoreGive(s_cache_lock);
    return count;
}

bool memo_api_item_copy(int index, memo_api_item_t *out)
{
    if (s_cache_lock == NULL || out == NULL) return false;
    xSemaphoreTake(s_cache_lock, portMAX_DELAY);
    bool valid = index >= 0 && index < s_count;
    if (valid) *out = s_items[index];
    xSemaphoreGive(s_cache_lock);
    return valid;
}

static bool valid_revision(const char *revision)
{
    if (revision == NULL || revision[0] == '\0') return true;
    if (strlen(revision) != 64) return false;
    for (const char *p = revision; *p; p++) {
        if (!((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f'))) return false;
    }
    return true;
}

esp_err_t memo_api_request_text(const char *id, uint32_t offset, bool previous, const char *revision)
{
    if (!valid_id(id) || !valid_revision(revision)) return ESP_ERR_INVALID_ARG;
    if (s_cache_lock == NULL) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_cache_lock, portMAX_DELAY);
    const bool different = strcmp(s_text.id, id) != 0;
    if (different) memset(&s_text, 0, sizeof(s_text));
    s_text_serial++;
    strcpy(s_text.id, id);
    s_text.pending = true; s_text.error = ESP_OK;
    s_text_query = (text_query_t){.offset=offset, .previous=previous};
    strcpy(s_text_query.id, id);
    if (revision != NULL) strcpy(s_text_query.revision, revision);
    xSemaphoreGive(s_cache_lock);
    return ESP_OK;
}

void memo_api_cancel_text(void)
{
    if (s_cache_lock == NULL) return;
    xSemaphoreTake(s_cache_lock, portMAX_DELAY);
    s_text_serial++;
    memset(&s_text, 0, sizeof(s_text));
    xSemaphoreGive(s_cache_lock);
}

bool memo_api_text_copy(memo_api_text_t *out)
{
    if (out == NULL || s_cache_lock == NULL) return false;
    xSemaphoreTake(s_cache_lock, portMAX_DELAY);
    *out = s_text;
    xSemaphoreGive(s_cache_lock);
    return out->id[0] != '\0';
}

void memo_api_poll_text(void)
{
    if (s_cache_lock == NULL) return;
    xSemaphoreTake(s_cache_lock, portMAX_DELAY);
    const bool pending = s_text.pending;
    const text_query_t query = s_text_query;
    const uint32_t serial = s_text_serial;
    xSemaphoreGive(s_cache_lock);
    if (!pending) return;

    char path[240];
    snprintf(path, sizeof(path), "/api/v1/memos/%s/text?limit=%u&offset=%u&direction=%s&revision=%s",
             query.id, MEMO_API_TEXT_BYTES, (unsigned)query.offset,
             query.previous ? "previous" : "next", query.revision);
    body_buf_t body = {0};
    const int code = http_get(path, &body);
    esp_err_t error = code == 409 ? ESP_ERR_INVALID_STATE : code == 404 ? ESP_ERR_NOT_FOUND :
        (code < 0 || code >= 500) ? ESP_FAIL : ESP_ERR_INVALID_RESPONSE;
    memo_api_text_t *next = NULL;
    if (code == 200) {
        error = ESP_ERR_INVALID_RESPONSE;
        cJSON *root = cJSON_Parse(body.data);
        const cJSON *id = cJSON_GetObjectItem(root, "id");
        const cJSON *status = cJSON_GetObjectItem(root, "status");
        const cJSON *revision = cJSON_GetObjectItem(root, "revision");
        const cJSON *text = cJSON_GetObjectItem(root, "text");
        next = calloc(1, sizeof(*next));
        if (next == NULL) error = ESP_ERR_NO_MEM;
        else if (cJSON_IsString(id) && strcmp(id->valuestring, query.id) == 0 &&
                 cJSON_IsString(status) && valid_status(status->valuestring) &&
                 cJSON_IsString(revision) && strlen(revision->valuestring) == 64 && valid_revision(revision->valuestring) &&
                 (query.revision[0] == '\0' || strcmp(query.revision, revision->valuestring) == 0) &&
                 cJSON_IsString(text) && strlen(text->valuestring) <= MEMO_API_TEXT_BYTES && memo_text_is_valid_utf8(text->valuestring) &&
                 json_u32(cJSON_GetObjectItem(root, "offset"), &next->offset) &&
                 json_u32(cJSON_GetObjectItem(root, "end"), &next->end) &&
                 json_u32(cJSON_GetObjectItem(root, "total"), &next->total) &&
                 next->offset <= next->end && next->end <= next->total &&
                 next->end - next->offset == strlen(text->valuestring) &&
                 (query.previous ? next->end == query.offset : next->offset == query.offset) &&
                 (next->end > next->offset || next->total == 0 ||
                  (query.previous ? query.offset == 0 : query.offset == next->total))) {
            strcpy(next->id, query.id);
            strcpy(next->status, status->valuestring);
            strcpy(next->revision, revision->valuestring);
            strcpy(next->text, text->valuestring);
            next->generation = serial;
            error = ESP_OK;
        }
        cJSON_Delete(root);
    }
    free(body.data);
    xSemaphoreTake(s_cache_lock, portMAX_DELAY);
    if (serial == s_text_serial && s_text.pending) {
        if (error == ESP_OK) s_text = *next;
        else {
            s_text.error = error;
            // Transport failures retry while connected; semantic/version errors
            // are handed back to the reader, not retried with an obsolete cursor.
            s_text.pending = error == ESP_FAIL;
        }
    }
    xSemaphoreGive(s_cache_lock);
    free(next);
}
