// main/memo_events.c —— SSE 客户端：订阅 /api/v1/events 并更新本地缓存。
#include "memo_events.h"

#include <stdio.h>
#include <errno.h>
#include <stdatomic.h>
#include <string.h>

#include "cJSON.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "memo_api.h"
#include "memo_discovery.h"
#include "memo_net.h"

static const char *TAG = "memo_events";

#define EVENTS_STACK 6144
#define EVENTS_RECONNECT_MS 5000
#define EVENTS_IDLE_MS 2000
#define EVENTS_BUF 512

static TaskHandle_t s_task;
static SemaphoreHandle_t s_done;
static atomic_bool s_running;
static atomic_bool s_connected;
static char s_line[EVENTS_BUF];
static size_t s_line_len;

/* 把一条 data: 中的 JSON 合并进缓存。 */
static void apply_event(const char *json)
{
    cJSON *root = cJSON_Parse(json);
    if (root == NULL) return;

    const cJSON *id = cJSON_GetObjectItem(root, "id");
    if (cJSON_IsString(id) && strlen(id->valuestring) == 36) {
        const cJSON *status = cJSON_GetObjectItem(root, "status");
        const cJSON *snippet = cJSON_GetObjectItem(root, "snippet");
        const cJSON *rec = cJSON_GetObjectItem(root, "recorded_at");
        const cJSON *dur = cJSON_GetObjectItem(root, "duration_ms");

        memo_api_upsert(id->valuestring,
                        cJSON_IsNumber(rec) ? (int64_t)rec->valuedouble : 0,
                        cJSON_IsNumber(dur) ? (uint32_t)dur->valuedouble : 0,
                        cJSON_IsString(status) ? status->valuestring : NULL,
                        cJSON_IsString(snippet) ? snippet->valuestring : NULL);
        ESP_LOGI(TAG, "event %s -> %s", id->valuestring,
                 cJSON_IsString(status) ? status->valuestring : "?");
    }
    cJSON_Delete(root);
}

/* 按行处理：空行表示一条事件结束，data: 行携带 JSON。 */
static void feed_bytes(const char *data, int len)
{
    static char payload[EVENTS_BUF];
    static size_t payload_len;

    for (int i = 0; i < len; i++) {
        const char c = data[i];
        if (c == '\r') continue;

        if (c == '\n') {
            s_line[s_line_len] = '\0';
            if (s_line_len == 0) {
                /* 事件结束 */
                if (payload_len > 0) {
                    payload[payload_len] = '\0';
                    apply_event(payload);
                    payload_len = 0;
                }
            } else if (strncmp(s_line, "data:", 5) == 0) {
                const char *value = s_line + 5;
                while (*value == ' ') value++;
                const size_t value_len = strlen(value);
                if (payload_len + value_len + 1 < sizeof(payload)) {
                    memcpy(payload + payload_len, value, value_len);
                    payload_len += value_len;
                }
            }
            s_line_len = 0;
            continue;
        }

        if (s_line_len + 1 < sizeof(s_line)) {
            s_line[s_line_len++] = c;
        }
    }
}

/* 连一次，直到流结束或出错。返回后由任务决定重连。 */
static void subscribe_once(void)
{
    char url[128];
    snprintf(url, sizeof(url), "http://%s:%u/api/v1/events", memo_discovery_host(),
             (unsigned)memo_discovery_port());

    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 1000, /* Bounded reads so stop can join before resource release. */
        .buffer_size = 1024,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == NULL) return;

    esp_http_client_set_header(client, "Accept", "text/event-stream");
    s_line_len = 0;
    if (s_running && esp_http_client_open(client, 0) == ESP_OK &&
        esp_http_client_fetch_headers(client) >= 0 &&
        esp_http_client_get_status_code(client) == 200) {
        s_connected = true;
        char bytes[256];
        unsigned idle_reads = 0;
        while (s_running) {
            errno = 0;
            int n = esp_http_client_read(client, bytes, sizeof(bytes));
            if (n > 0) {
                feed_bytes(bytes, n);
                idle_reads = 0;
            } else if (n == -ESP_ERR_HTTP_EAGAIN || n == ESP_ERR_HTTP_EAGAIN ||
                       errno == EAGAIN || errno == ETIMEDOUT) {
                if (++idle_reads >= 60) break;
            } else break;
        }
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    s_connected = false;
}

static void interruptible_delay(uint32_t ms)
{
    while (s_running && ms > 0) {
        const uint32_t slice = ms > 100 ? 100 : ms;
        vTaskDelay(pdMS_TO_TICKS(slice));
        ms -= slice;
    }
}

static void events_task(void *arg)
{
    (void)arg;

    while (s_running) {
        if (!memo_net_has_ip()) {
            interruptible_delay(EVENTS_IDLE_MS);
            continue;
        }
        if (memo_discovery_state() != MEMO_DISCOVERY_FOUND) {
            interruptible_delay(EVENTS_IDLE_MS);
            continue;
        }

        subscribe_once();

        if (!s_running) break;
        interruptible_delay(EVENTS_RECONNECT_MS);
    }

    s_connected = false;
    if (s_done != NULL) xSemaphoreGive(s_done);
    vTaskDelete(NULL);
}

esp_err_t memo_events_start(void)
{
    if (s_running) return ESP_OK;
    if (s_task != NULL) {
        /* A previous timeout may now have completed. Join non-blockingly before
         * allowing a new worker; never discard an unacknowledged handle. */
        if (s_done == NULL || xSemaphoreTake(s_done, 0) != pdTRUE) return ESP_ERR_INVALID_STATE;
        s_task = NULL;
    }

    if (s_done == NULL) {
        s_done = xSemaphoreCreateBinary();
        if (s_done == NULL) return ESP_ERR_NO_MEM;
    } else {
        xSemaphoreTake(s_done, 0);
    }

    s_running = true;
    if (xTaskCreate(events_task, "memo_events", EVENTS_STACK, NULL, 3, &s_task) != pdPASS) {
        s_running = false;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "subscriber started");
    return ESP_OK;
}

esp_err_t memo_events_stop(void)
{
    if (s_task == NULL) return ESP_OK;
    s_running = false;
    if (s_done != NULL && xSemaphoreTake(s_done, pdMS_TO_TICKS(3000)) != pdTRUE) {
        ESP_LOGW(TAG, "subscriber did not stop in time");
        return ESP_ERR_TIMEOUT;
    }
    s_task = NULL;
    return ESP_OK;
}

bool memo_events_connected(void)
{
    return s_connected;
}
