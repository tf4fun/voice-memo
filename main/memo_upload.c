// main/memo_upload.c —— 单条上传任务：发现服务端 → POST /api/v1/memos。
#include "memo_upload.h"

#include <stdio.h>
#include <stdatomic.h>
#include <string.h>

#include "esp_err.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "memo_api.h"
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
static atomic_bool s_running;
static atomic_bool s_rediscover_requested;
static unsigned s_idle_ticks = 30; /* 首次空闲就立刻拉一次列表 */
static bool s_hot;                 /* 刚上传完：转写结果很快会回来 */

/* 空闲轮询：热窗口内 2 秒一次，否则 60 秒一次。 */
#define IDLE_TICKS_HOT 1
#define IDLE_TICKS_COLD 30
static char s_error[64];
static char s_device_id[13];
static uint32_t s_backoff_ms = UPLOAD_RETRY_MIN_MS;
static SemaphoreHandle_t s_review_lock;
static memo_upload_rejection_t s_review;
static bool s_review_delete;
static bool s_capture_busy;
static bool s_playback_busy;
static char s_attempt_id[37];
static memo_upload_policy_t s_policy;

static bool capture_busy(void)
{
    xSemaphoreTake(s_review_lock, portMAX_DELAY);
    bool busy = s_capture_busy || s_playback_busy || memo_spool_recovery_blocked();
    xSemaphoreGive(s_review_lock);
    if (!busy) {
        memo_spool_entry_t entries[MEMO_SPOOL_MAX_ENTRIES];
        const int count = memo_spool_scan(entries, MEMO_SPOOL_MAX_ENTRIES);
        for (int i = 0; i < count && i < MEMO_SPOOL_MAX_ENTRIES; i++) {
            if (entries[i].state == MEMO_SPOOL_RECORDING || entries[i].state == MEMO_SPOOL_DRAFT) {
                busy = true;
                break;
            }
        }
    }
    return busy || memo_spool_recovery_blocked();
}

void memo_upload_set_capture_busy(bool busy)
{
    if (s_review_lock != NULL) xSemaphoreTake(s_review_lock, portMAX_DELAY);
    s_capture_busy = busy;
    if (s_review_lock != NULL) xSemaphoreGive(s_review_lock);
}

void memo_upload_set_playback_busy(bool busy)
{
    if (s_review_lock != NULL) xSemaphoreTake(s_review_lock, portMAX_DELAY);
    s_playback_busy = busy;
    if (s_review_lock != NULL) xSemaphoreGive(s_review_lock);
}

bool memo_upload_rejection(memo_upload_rejection_t *out)
{
    if (out == NULL || s_review_lock == NULL) return false;
    xSemaphoreTake(s_review_lock, portMAX_DELAY);
    *out = s_review;
    xSemaphoreGive(s_review_lock);
    return out->id[0] != '\0';
}

esp_err_t memo_upload_choose(const char *id, bool delete_recording)
{
    if (id == NULL || s_review_lock == NULL) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s_review_lock, portMAX_DELAY);
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (s_review.id[0] != '\0' && strcmp(id, s_review.id) == 0 && !s_review.deciding) {
        s_review.deciding = true;
        s_review.operation_failed = false;
        s_review_delete = delete_recording;
        err = ESP_OK;
    }
    xSemaphoreGive(s_review_lock);
    return err;
}

/* Runs even offline. Disk operations belong to this worker, never a timer or
 * button callback. A failed operation keeps the dialog and the file intact. */
static bool review_rejected(void)
{
    memo_upload_rejection_t review;
    if (memo_upload_rejection(&review)) {
        bool completed = false;
        if (review.deciding) {
            xSemaphoreTake(s_review_lock, portMAX_DELAY);
            const bool deleting = s_review_delete;
            xSemaphoreGive(s_review_lock);
            const memo_spool_state_t source = review.legacy_kept
                ? MEMO_SPOOL_LEGACY_KEPT : MEMO_SPOOL_BAD;
            const esp_err_t err = deleting
                ? memo_spool_remove(review.id, source)
                : memo_spool_move(review.id, source, MEMO_SPOOL_PENDING);
            xSemaphoreTake(s_review_lock, portMAX_DELAY);
            if (err == ESP_OK) {
                s_attempt_id[0] = '\0';
                s_policy = (memo_upload_policy_t){0};
                s_backoff_ms = UPLOAD_RETRY_MIN_MS;
                memset(&s_review, 0, sizeof(s_review));
                completed = true;
            } else {
                s_review.deciding = false;
                s_review.operation_failed = true;
            }
            xSemaphoreGive(s_review_lock);
        }
        s_state = completed ? MEMO_UPLOAD_IDLE : MEMO_UPLOAD_REVIEW;
        return true;
    }

    memo_spool_entry_t entries[MEMO_SPOOL_MAX_ENTRIES];
    const int count = memo_spool_scan(entries, MEMO_SPOOL_MAX_ENTRIES);
    /* Drain only one active item. Extra legacy files are preserved but must
     * not take the dialog away from the recording the user just retried. */
    for (int i = 0; i < count && i < MEMO_SPOOL_MAX_ENTRIES; i++) {
        if (entries[i].state == MEMO_SPOOL_PENDING) return false;
    }
    for (int i = 0; i < count && i < MEMO_SPOOL_MAX_ENTRIES; i++) {
        if (entries[i].state != MEMO_SPOOL_BAD &&
            entries[i].state != MEMO_SPOOL_LEGACY_KEPT) continue;
        xSemaphoreTake(s_review_lock, portMAX_DELAY);
        memset(&s_review, 0, sizeof(s_review));
        snprintf(s_review.id, sizeof(s_review.id), "%s", entries[i].id);
        s_review.recorded_at = entries[i].recorded_at;
        s_review.duration_ms = entries[i].duration_ms;
        s_review.legacy_kept = entries[i].state == MEMO_SPOOL_LEGACY_KEPT;
        xSemaphoreGive(s_review_lock);
        s_state = MEMO_UPLOAD_REVIEW;
        return true;
    }
    return false;
}

static bool reject_entry(const memo_spool_entry_t *entry)
{
    if (!memo_upload_policy_rejected(&s_policy)) return false;
    /* Rename only; deletion always needs the user's choice. If rename fails,
     * the saturated budget prevents any additional HTTP request. */
    if (memo_spool_set_state(entry->id, MEMO_SPOOL_BAD) != ESP_OK) return false;
    xSemaphoreTake(s_review_lock, portMAX_DELAY);
    s_review = (memo_upload_rejection_t){0};
    snprintf(s_review.id, sizeof(s_review.id), "%s", entry->id);
    s_review.recorded_at = entry->recorded_at;
    s_review.duration_ms = entry->duration_ms;
    xSemaphoreGive(s_review_lock);
    s_state = MEMO_UPLOAD_REVIEW;
    return true;
}

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

static bool ensure_endpoint(memo_endpoint_t *endpoint)
{
    if (!memo_discovery_endpoint(endpoint) || !endpoint->confirmed) {
        if (memo_api_probe() != ESP_OK || !memo_discovery_endpoint(endpoint) || !endpoint->confirmed) {
            set_error("no verified server");
            return false;
        }
    }
    return true;
}

static bool exceeds_server_limits(const memo_spool_entry_t *entry)
{
    const uint32_t server_max = memo_api_max_memo_ms();
    const uint32_t server_bytes = memo_api_max_bytes();
    if ((server_max > 0 && entry->duration_ms > server_max) ||
        (server_bytes > 0 && entry->size > server_bytes)) {
        ESP_LOGE(TAG, "memo exceeds server limits (duration %u/%u ms, bytes %u/%u)",
                 (unsigned)entry->duration_ms, (unsigned)server_max,
                 (unsigned)entry->size, (unsigned)server_bytes);
        return true;
    }
    return false;
}

static size_t stream_body(esp_http_client_handle_t client, FILE *file, size_t total)
{
    /* 直接流式发送文件体：不把整条录音读进 RAM。 */
    char chunk[1024];
    size_t sent = 0;
    while (s_running && sent < total) {
        const size_t want = (total - sent) < sizeof(chunk) ? (total - sent) : sizeof(chunk);
        const size_t got = fread(chunk, 1, want, file);
        if (got == 0) break;
        if (esp_http_client_write(client, chunk, got) != (int)got) {
            set_error("write failed");
            break;
        }
        sent += got;
    }
    return sent;
}

static bool upload_accepted(const memo_spool_entry_t *entry, int code)
{
    ESP_LOGI(TAG, "uploaded %s (%u bytes), status %d", entry->id,
             (unsigned)entry->size, code);
    // A local reader may have opened while POST was in flight. Keep its
    // source until player join; a later idempotent POST releases the file.
    if (!capture_busy()) memo_spool_remove(entry->id, MEMO_SPOOL_PENDING);
    memo_api_refresh(); /* 让列表立刻能看到这条的转写结果 */
    s_hot = true;       /* 接下来几秒内转写就会完成，密切轮询 */
    s_state = MEMO_UPLOAD_IDLE;
    s_backoff_ms = UPLOAD_RETRY_MIN_MS;
    s_attempt_id[0] = '\0';
    s_policy = (memo_upload_policy_t){0};
    return true;
}

static bool upload_failed_status(const memo_spool_entry_t *entry, memo_endpoint_t *endpoint, int code)
{
    if (code >= 400 && code < 500) {
        /* Three rejected attempts in total, then wait for a user decision. */
        ESP_LOGE(TAG, "server rejected %s with %d", entry->id, code);
        snprintf(s_error, sizeof(s_error), "rejected (%d)", code);
        s_state = MEMO_UPLOAD_FAILED;
        return reject_entry(entry);
    }

    if (code <= 0 || code >= 500) memo_discovery_invalidate(endpoint);
    ESP_LOGW(TAG, "upload failed with status %d, retrying", code);
    snprintf(s_error, sizeof(s_error), "http %d", code);
    s_state = MEMO_UPLOAD_FAILED;
    return false;
}

/* 返回 true 表示可以接着处理下一条，false 表示需要退避等待。 */
static bool upload_once(const memo_spool_entry_t *entry)
{
    if (strcmp(s_attempt_id, entry->id) != 0) {
        snprintf(s_attempt_id, sizeof(s_attempt_id), "%s", entry->id);
        s_policy = (memo_upload_policy_t){0};
        s_backoff_ms = UPLOAD_RETRY_MIN_MS;
    }
    if (s_policy.rejections == MEMO_UPLOAD_REJECTION_ATTEMPTS) return reject_entry(entry);
    memo_endpoint_t endpoint;
    if (!ensure_endpoint(&endpoint)) return false;

    if (exceeds_server_limits(entry)) {
        snprintf(s_error, sizeof(s_error), "exceeds server limits");
        s_state = MEMO_UPLOAD_FAILED;
        return reject_entry(entry);
    }

    char path[64];
    snprintf(path, sizeof(path), "%s/%s.memo", MEMO_SPOOL_MOUNT_POINT, entry->id);

    char url[128];
    snprintf(url, sizeof(url), "http://%s:%u/api/v1/memos",
             endpoint.host, (unsigned)endpoint.port);

    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 3000,
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
        memo_discovery_invalidate(&endpoint);
        fclose(file);
        esp_http_client_cleanup(client);
        return false;
    }

    const size_t sent = stream_body(client, file, entry->size);
    fclose(file);

    if (sent != entry->size) {
        if (s_running) memo_discovery_invalidate(&endpoint);
        esp_http_client_cleanup(client);
        return false;
    }

    s_state = MEMO_UPLOAD_SENDING;
    const int status = esp_http_client_fetch_headers(client);
    (void)status;
    const int code = esp_http_client_get_status_code(client);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (code == 200 || code == 201) return upload_accepted(entry, code);

    return upload_failed_status(entry, &endpoint, code);
}

void memo_upload_request_discovery(void)
{
    atomic_store(&s_rediscover_requested, true);
}

bool memo_upload_discovery_pending(void)
{
    return atomic_load(&s_rediscover_requested);
}

static void interruptible_delay(uint32_t ms)
{
    /* Keep short capture/review waits bounded without spinning on a queued
     * request. A manual request can interrupt network retry backoff. */
    const bool can_wake = ms > 200;
    while (s_running && ms > 0) {
        if (can_wake && memo_net_has_ip() && memo_upload_discovery_pending()) return;
        const uint32_t slice = ms > 100 ? 100 : ms;
        vTaskDelay(pdMS_TO_TICKS(slice));
        ms -= slice;
    }
}

static void upload_task(void *arg)
{
    (void)arg;
    derive_device_id();
    ESP_LOGI(TAG, "uploader running, device id %s", s_device_id);
    bool primed = false;

    while (s_running) {
        if (capture_busy() || review_rejected()) {
            interruptible_delay(200);
            continue;
        }
        /* The uploader is the only discovery/API writer. UI requests remain
         * queued offline and while capture owns the resources. Never clear the
         * metadata cache when changing endpoints. */
        if (memo_net_has_ip() && atomic_exchange(&s_rediscover_requested, false)) {
            memo_endpoint_t endpoint;
            if (memo_discovery_endpoint(&endpoint)) memo_discovery_invalidate(&endpoint);
            primed = false;
        }
        /* 一有网就先拉一次整表：否则缓存是空的，上传完成那一刻列表会"只剩一条"。 */
        if ((!primed || memo_discovery_state() != MEMO_DISCOVERY_FOUND) && memo_net_has_ip()) {
            /* 先确认这条 mDNS 记录真的可用，再谈上传。 */
            if (memo_api_probe() == ESP_OK) {
                primed = memo_api_refresh() == ESP_OK;
            } else {
                primed = false;
                interruptible_delay(UPLOAD_POLL_MS);
                continue;
            }
        }

        if (memo_net_has_ip() && memo_api_take_refresh_request()) {
            if (memo_api_refresh() != ESP_OK) memo_api_request_refresh();
        }
        if (memo_net_has_ip()) memo_api_poll_text();
        memo_spool_entry_t entry;
        if (!pending_entry(&entry)) {
            s_state = MEMO_UPLOAD_IDLE;
            /* 轮询列表：刚上传完的那几秒用快节奏等转写，之后退回低频心跳。 */
            const unsigned threshold = s_hot ? IDLE_TICKS_HOT : IDLE_TICKS_COLD;
            if (memo_net_has_ip() && ++s_idle_ticks >= threshold) {
                s_idle_ticks = 0;
                if (memo_api_refresh() == ESP_OK && memo_api_all_terminal()) {
                    s_hot = false;
                }
            }
            interruptible_delay(UPLOAD_POLL_MS);
            continue;
        }

        if (!memo_net_has_ip()) {
            s_state = MEMO_UPLOAD_DISCOVERING;
            set_error("no network");
            interruptible_delay(UPLOAD_POLL_MS);
            continue;
        }

        /* Capture may have started while discovery/probing was blocked. */
        if (!s_running || capture_busy()) continue;
        if (!upload_once(&entry)) {
            ESP_LOGW(TAG, "retry in %u ms (%s)", (unsigned)s_backoff_ms, s_error);
            interruptible_delay(s_backoff_ms);
            s_backoff_ms = s_backoff_ms * 2 > UPLOAD_RETRY_MAX_MS ? UPLOAD_RETRY_MAX_MS
                                                                 : s_backoff_ms * 2;
        }
    }

    s_state = MEMO_UPLOAD_IDLE;
    if (s_done != NULL) xSemaphoreGive(s_done);
    vTaskSuspend(NULL); // Joined/deleted by the owner; no idle-task cleanup delay.
}

esp_err_t memo_upload_start(void)
{
    if (s_running) return ESP_OK;
    if (s_task != NULL) {
        /* A previous timeout may now have completed. Join non-blockingly before
         * allowing a new worker; never discard an unacknowledged handle. */
        if (s_done == NULL || xSemaphoreTake(s_done, 0) != pdTRUE) return ESP_ERR_INVALID_STATE;
        vTaskDelete(s_task);
        s_task = NULL;
    }

    if (s_review_lock == NULL) {
        s_review_lock = xSemaphoreCreateMutex();
        if (s_review_lock == NULL) return ESP_ERR_NO_MEM;
    }

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
    ESP_LOGI(TAG, "uploader task created");
    return ESP_OK;
}

esp_err_t memo_upload_stop(void)
{
    if (s_task == NULL) return ESP_OK;
    s_running = false;
    if (s_done != NULL && xSemaphoreTake(s_done, pdMS_TO_TICKS(5000)) != pdTRUE) {
        ESP_LOGW(TAG, "uploader did not stop in time");
        return ESP_ERR_TIMEOUT;
    }
    vTaskDelete(s_task);
    s_task = NULL;
    return ESP_OK;
}

memo_upload_state_t memo_upload_state(void)
{
    return s_state;
}

bool memo_upload_pending(void)
{
    if (memo_spool_recovery_blocked()) return true;
    memo_spool_entry_t entries[MEMO_SPOOL_MAX_ENTRIES];
    const int count = memo_spool_scan(entries, MEMO_SPOOL_MAX_ENTRIES);
    for (int i = 0; i < count && i < MEMO_SPOOL_MAX_ENTRIES; i++) {
        if (entries[i].state == MEMO_SPOOL_PENDING || entries[i].state == MEMO_SPOOL_BAD ||
            entries[i].state == MEMO_SPOOL_LEGACY_KEPT ||
            entries[i].state == MEMO_SPOOL_RECORDING || entries[i].state == MEMO_SPOOL_DRAFT) return true;
    }
    return memo_spool_recovery_blocked();
}

const char *memo_upload_error(void)
{
    return s_error;
}
