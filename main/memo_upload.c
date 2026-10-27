// main/memo_upload.c —— 单条上传任务：发现服务端 → POST /api/v1/memos。
#include "memo_upload.h"

#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "memo_discovery.h"
#include "memo_net.h"
#include "memo_spool.h"

static const char *TAG = "memo_upload";

#define UPLOAD_TASK_STACK 8192
#define UPLOAD_POLL_MS 2000
#define UPLOAD_RETRY_MIN_MS 5000
#define UPLOAD_RETRY_MAX_MS 120000

static volatile memo_upload_state_t s_state = MEMO_UPLOAD_IDLE;
static TaskHandle_t s_task;
static SemaphoreHandle_t s_done;
static volatile bool s_running;
static char s_error[64];
static char s_device_id[13];
static uint32_t s_backoff_ms = UPLOAD_RETRY_MIN_MS;

static void set_error(const char *text)
{
    strncpy(s_error, text, sizeof(s_error) - 1);
    s_error[sizeof(s_error) - 1] = '\0';
}

static void derive_device_id(void)
{
    if (s_device_id[0] != '\0') return;
    uint8_t mac[6] = {0};
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK) {
        snprintf(s_device_id, sizeof(s_device_id), "%02X%02X%02X%02X%02X%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }
}

/* 找到第一条待上传录音；没有则返回 false。 */
static bool pending_entry(memo_spool_entry_t *out)
{
    memo_spool_entry_t entries[MEMO_SPOOL_MAX_ENTRIES];
    const int total = memo_spool_scan(entries, MEMO_SPOOL_MAX_ENTRIES);
    for (int i = 0; i < total && i < MEMO_SPOOL_MAX_ENTRIES; i++) {
        if (entries[i].state == MEMO_SPOOL_PENDING) {
            *out = entries[i];
            return true;
        }
    }
    return false;
}

/* 返回 true 表示可以接着处理下一条，false 表示需要退避等待。 */
static bool upload_once(const memo_spool_entry_t *entry)
{
    if (memo_discovery_state() != MEMO_DISCOVERY_FOUND) {
        s_state = MEMO_UPLOAD_DISCOVERING;
        if (memo_discovery_search(3000) != ESP_OK) {
            set_error("server not found");
            return false;
        }
    }

    char path[64];
    snprintf(path, sizeof(path), "%s/%s.memo", MEMO_SPOOL_MOUNT_POINT, entry->id);

    char url[128];
    snprintf(url, sizeof(url), "http://%s:%u/api/v1/memos",
             memo_discovery_host(), (unsigned)memo_discovery_port());

    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 30000,
        .buffer_size = 4096,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == NULL) {
        set_error("http init failed");
        return false;
    }

    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        set_error("cannot open recording");
        esp_http_client_cleanup(client);
        return false;
    }

    esp_http_client_set_header(client, "Content-Type", "application/octet-stream");
    esp_http_client_set_header(client, "X-Device-Id", s_device_id);
    esp_http_client_set_header(client, "X-Memo-Id", entry->id);

    esp_err_t err = esp_http_client_open(client, entry->size);
    if (err != ESP_OK) {
        set_error(esp_err_to_name(err));
        fclose(file);
        esp_http_client_cleanup(client);
        return false;
    }

    /* 直接流式发送文件体：不把整条录音读进 RAM。 */
    char chunk[1024];
    size_t sent = 0;
    while (sent < entry->size) {
        const size_t want = (entry->size - sent) < sizeof(chunk) ? (entry->size - sent) : sizeof(chunk);
        const size_t got = fread(chunk, 1, want, file);
        if (got == 0) break;
        if (esp_http_client_write(client, chunk, got) != (int)got) {
            set_error("write failed");
            break;
        }
        sent += got;
    }
    fclose(file);

    if (sent != entry->size) {
        esp_http_client_cleanup(client);
        return false;
    }

    s_state = MEMO_UPLOAD_SENDING;
    const int status = esp_http_client_fetch_headers(client);
    (void)status;
    const int code = esp_http_client_get_status_code(client);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (code == 200 || code == 201) {
        ESP_LOGI(TAG, "uploaded %s (%u bytes), status %d", entry->id,
                 (unsigned)entry->size, code);
        memo_spool_remove(entry->id, MEMO_SPOOL_PENDING);
        s_state = MEMO_UPLOAD_IDLE;
        s_backoff_ms = UPLOAD_RETRY_MIN_MS;
        return true;
    }

    if (code >= 400 && code < 500) {
        /* 服务端明确拒绝：标记为坏文件，不要无限重试。 */
        ESP_LOGE(TAG, "server rejected %s with %d", entry->id, code);
        snprintf(s_error, sizeof(s_error), "rejected (%d)", code);
        memo_spool_set_state(entry->id, MEMO_SPOOL_BAD);
        s_state = MEMO_UPLOAD_FAILED;
        return true;
    }

    ESP_LOGW(TAG, "upload failed with status %d, retrying", code);
    snprintf(s_error, sizeof(s_error), "http %d", code);
    s_state = MEMO_UPLOAD_FAILED;
    return false;
}

static void upload_task(void *arg)
{
    (void)arg;
    derive_device_id();

    while (s_running) {
        memo_spool_entry_t entry;
        if (!pending_entry(&entry)) {
            s_state = MEMO_UPLOAD_IDLE;
            vTaskDelay(pdMS_TO_TICKS(UPLOAD_POLL_MS));
            continue;
        }

        if (!memo_net_has_ip()) {
            s_state = MEMO_UPLOAD_DISCOVERING;
            set_error("no network");
            vTaskDelay(pdMS_TO_TICKS(UPLOAD_POLL_MS));
            continue;
        }

        if (!upload_once(&entry)) {
            ESP_LOGW(TAG, "retry in %u ms (%s)", (unsigned)s_backoff_ms, s_error);
            vTaskDelay(pdMS_TO_TICKS(s_backoff_ms));
            s_backoff_ms = s_backoff_ms * 2 > UPLOAD_RETRY_MAX_MS ? UPLOAD_RETRY_MAX_MS
                                                                 : s_backoff_ms * 2;
        }
    }

    s_state = MEMO_UPLOAD_IDLE;
    if (s_done != NULL) xSemaphoreGive(s_done);
    vTaskDelete(NULL);
}

esp_err_t memo_upload_start(void)
{
    if (s_running) return ESP_OK;

    if (s_done == NULL) {
        s_done = xSemaphoreCreateBinary();
        if (s_done == NULL) return ESP_ERR_NO_MEM;
    } else {
        xSemaphoreTake(s_done, 0);
    }

    s_running = true;
    if (xTaskCreate(upload_task, "memo_upload", UPLOAD_TASK_STACK, NULL, 4, &s_task) != pdPASS) {
        s_running = false;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "uploader started (device %s)", s_device_id);
    return ESP_OK;
}

void memo_upload_stop(void)
{
    if (!s_running) return;
    s_running = false;
    if (s_done != NULL && xSemaphoreTake(s_done, pdMS_TO_TICKS(5000)) != pdTRUE) {
        ESP_LOGW(TAG, "uploader did not stop in time");
    }
    s_task = NULL;
}

memo_upload_state_t memo_upload_state(void)
{
    return s_state;
}

bool memo_upload_pending(void)
{
    memo_spool_entry_t entry;
    return pending_entry(&entry);
}

const char *memo_upload_error(void)
{
    return s_error;
}
