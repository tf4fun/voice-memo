// main/memo_api.c —— 服务端 HTTP 客户端。
#include "memo_api.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_http_client.h"
#include "esp_log.h"

#include "memo_discovery.h"

static const char *TAG = "memo_api";

static memo_api_item_t s_items[MEMO_API_MAX_ITEMS];
static int s_count;
static volatile bool s_updated;
static bool s_probed;
static uint32_t s_max_memo_ms;

/* 响应体缓冲：列表可能几十 KB，按块累积。 */
typedef struct {
    char *data;
    size_t len;
    size_t cap;
} body_buf_t;

static esp_err_t body_append(body_buf_t *buf, const char *chunk, int chunk_len)
{
    if (buf->len + (size_t)chunk_len + 1 > buf->cap) {
        size_t cap = buf->cap == 0 ? 4096 : buf->cap;
        while (cap < buf->len + (size_t)chunk_len + 1) cap *= 2;
        char *grown = realloc(buf->data, cap);
        if (grown == NULL) return ESP_ERR_NO_MEM;
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
        body_append(buf, (const char *)evt->data, evt->data_len);
    }
    return ESP_OK;
}

/* 组一个指向服务端的 URL。 */
static esp_err_t build_url(char *out, size_t cap, const char *path)
{
    if (memo_discovery_state() != MEMO_DISCOVERY_FOUND) {
        if (memo_discovery_search(3000) != ESP_OK) return ESP_ERR_NOT_FOUND;
    }
    snprintf(out, cap, "http://%s:%u%s", memo_discovery_host(),
             (unsigned)memo_discovery_port(), path);
    return ESP_OK;
}

/* 一次 GET，把响应体读到 buf；成功时返回 HTTP 状态码。 */
static int http_get(const char *path, body_buf_t *buf)
{
    char url[128];
    if (build_url(url, sizeof(url), path) != ESP_OK) return -1;

    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 15000,
        .buffer_size = 2048,
        .event_handler = http_event,
        .user_data = buf,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == NULL) return -1;

    int code = -1;
    if (esp_http_client_perform(client) == ESP_OK) {
        code = esp_http_client_get_status_code(client);
    }
    esp_http_client_cleanup(client);
    return code;
}

esp_err_t memo_api_probe(void)
{
    body_buf_t ping = {0};
    const int ping_code = http_get("/api/v1/ping", &ping);
    free(ping.data);
    if (ping_code != 200 && ping_code != 204) {
        ESP_LOGW(TAG, "ping failed (http %d)", ping_code);
        return ESP_FAIL;
    }

    body_buf_t info = {0};
    const int info_code = http_get("/api/v1/server", &info);
    if (info_code != 200) {
        ESP_LOGW(TAG, "server info failed (http %d)", info_code);
        free(info.data);
        s_probed = true; /* ping 通了也算可用，限值按本地策略 */
        return ESP_OK;
    }

    cJSON *root = cJSON_Parse(info.data);
    free(info.data);
    if (root != NULL) {
        const cJSON *limits = cJSON_GetObjectItem(root, "limits");
        const cJSON *max_ms = cJSON_GetObjectItem(limits, "max_memo_ms");
        const cJSON *max_bytes = cJSON_GetObjectItem(limits, "max_bytes");
        if (cJSON_IsNumber(max_ms)) s_max_memo_ms = (uint32_t)max_ms->valuedouble;
        ESP_LOGI(TAG, "server limits: max_memo_ms=%u max_bytes=%d",
                 (unsigned)s_max_memo_ms,
                 cJSON_IsNumber(max_bytes) ? (int)max_bytes->valuedouble : -1);
        cJSON_Delete(root);
    }
    s_probed = true;
    return ESP_OK;
}

uint32_t memo_api_max_memo_ms(void)
{
    return s_max_memo_ms;
}

esp_err_t memo_api_refresh(void)
{
    body_buf_t buf = {0};
    const int code = http_get("/api/v1/memos?limit=24", &buf);
    if (code != 200) {
        ESP_LOGW(TAG, "list failed (http %d)", code);
        free(buf.data);
        return ESP_FAIL;
    }

    cJSON *root = cJSON_Parse(buf.data);
    free(buf.data);
    if (root == NULL) {
        ESP_LOGW(TAG, "list is not valid JSON");
        return ESP_ERR_INVALID_RESPONSE;
    }

    const cJSON *items = cJSON_GetObjectItem(root, "items");
    if (!cJSON_IsArray(items)) {
        /* 响应结构不对：保留上一份缓存，而不是把列表清空。 */
        ESP_LOGW(TAG, "list response has no items array");
        cJSON_Delete(root);
        return ESP_ERR_INVALID_RESPONSE;
    }
    s_count = 0;
    const cJSON *it = NULL;
    cJSON_ArrayForEach(it, items) {
        if (s_count >= MEMO_API_MAX_ITEMS) break;

        const cJSON *id = cJSON_GetObjectItem(it, "id");
        if (!cJSON_IsString(id) || strlen(id->valuestring) != 36) continue;

        memo_api_item_t *item = &s_items[s_count];
        memset(item, 0, sizeof(*item));
        strncpy(item->id, id->valuestring, sizeof(item->id) - 1);

        const cJSON *rec = cJSON_GetObjectItem(it, "recorded_at");
        if (cJSON_IsNumber(rec)) item->recorded_at = (int64_t)rec->valuedouble;

        const cJSON *dur = cJSON_GetObjectItem(it, "duration_ms");
        if (cJSON_IsNumber(dur)) item->duration_ms = (uint32_t)dur->valuedouble;

        const cJSON *status = cJSON_GetObjectItem(it, "status");
        if (cJSON_IsString(status)) {
            strncpy(item->status, status->valuestring, sizeof(item->status) - 1);
        }

        const cJSON *snip = cJSON_GetObjectItem(it, "snippet");
        if (cJSON_IsString(snip)) {
            strncpy(item->snippet, snip->valuestring, sizeof(item->snippet) - 1);
        }
        s_count++;
    }
    cJSON_Delete(root);

    s_updated = true;
    ESP_LOGI(TAG, "list: %d item(s)", s_count);
    return ESP_OK;
}

bool memo_api_take_updated(void)
{
    const bool updated = s_updated;
    s_updated = false;
    return updated;
}

void memo_api_upsert(const char *id, int64_t recorded_at, uint32_t duration_ms,
                     const char *status, const char *snippet)
{
    if (id == NULL || strlen(id) != 36) return;

    int index = -1;
    for (int i = 0; i < s_count; i++) {
        if (strcmp(s_items[i].id, id) == 0) {
            index = i;
            break;
        }
    }
    if (index < 0) {
        if (s_count >= MEMO_API_MAX_ITEMS) return;
        index = s_count++;
        memset(&s_items[index], 0, sizeof(s_items[index]));
        strncpy(s_items[index].id, id, sizeof(s_items[index].id) - 1);
    }

    memo_api_item_t *item = &s_items[index];
    if (recorded_at > 0) item->recorded_at = recorded_at;
    if (duration_ms > 0) item->duration_ms = duration_ms;
    if (status != NULL) {
        strncpy(item->status, status, sizeof(item->status) - 1);
    }
    if (snippet != NULL && snippet[0] != '\0') {
        strncpy(item->snippet, snippet, sizeof(item->snippet) - 1);
    }
    s_updated = true;
}

bool memo_api_all_terminal(void)
{
    for (int i = 0; i < s_count; i++) {
        if (strcmp(s_items[i].status, "ready") != 0 &&
            strcmp(s_items[i].status, "failed") != 0) {
            return false;
        }
    }
    return true;
}

int memo_api_count(void)
{
    return s_count;
}

const memo_api_item_t *memo_api_item(int index)
{
    if (index < 0 || index >= s_count) return NULL;
    return &s_items[index];
}

esp_err_t memo_api_fetch_transcript(const char *id, char *out, size_t cap)
{
    if (out == NULL || cap == 0) return ESP_ERR_INVALID_ARG;
    out[0] = '\0';

    char path[64];
    snprintf(path, sizeof(path), "/api/v1/memos/%s", id);

    body_buf_t buf = {0};
    const int code = http_get(path, &buf);
    if (code != 200) {
        ESP_LOGW(TAG, "detail %s failed (http %d)", id, code);
        free(buf.data);
        return ESP_FAIL;
    }

    cJSON *root = cJSON_Parse(buf.data);
    free(buf.data);
    if (root == NULL) return ESP_ERR_INVALID_RESPONSE;

    esp_err_t err = ESP_ERR_NOT_FOUND;
    const cJSON *tr = cJSON_GetObjectItem(root, "transcript");
    if (cJSON_IsString(tr) && tr->valuestring[0] != '\0') {
        strncpy(out, tr->valuestring, cap - 1);
        out[cap - 1] = '\0';
        err = ESP_OK;
    }
    cJSON_Delete(root);
    return err;
}

esp_err_t memo_api_download_audio(const char *id, const char *path)
{
    char req[64];
    snprintf(req, sizeof(req), "/api/v1/memos/%s/audio", id);

    char url[128];
    if (build_url(url, sizeof(url), req) != ESP_OK) return ESP_ERR_NOT_FOUND;

    FILE *file = fopen(path, "wb");
    if (file == NULL) return ESP_FAIL;

    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 20000,
        .buffer_size = 2048,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == NULL) {
        fclose(file);
        return ESP_FAIL;
    }

    esp_err_t err = ESP_FAIL;
    if (esp_http_client_open(client, 0) == ESP_OK) {
        esp_http_client_fetch_headers(client);
        char chunk[1024];
        int total = 0;
        if (esp_http_client_get_status_code(client) == 200) {
            int read;
            while ((read = esp_http_client_read(client, chunk, sizeof(chunk))) > 0) {
                if (fwrite(chunk, 1, (size_t)read, file) != (size_t)read) {
                    total = -1;
                    break;
                }
                total += read;
            }
            if (total > 0) {
                ESP_LOGI(TAG, "downloaded %d bytes to %s", total, path);
                err = ESP_OK;
            }
        }
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    fclose(file);
    return err;
}
