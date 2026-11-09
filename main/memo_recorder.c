// main/memo_recorder.c —— Opus 录音任务。
//
// 参数与 docs/technical/client.md §7 一致：16 kHz 单声道、60 ms 帧、20 kbps、
// complexity 0、DTX 关闭（录音存档要求每帧齐全）。
#include "memo_recorder.h"

#include <stdlib.h>
#include <stdatomic.h>
#include <string.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_timer.h"

#include "bsp_audio.h"
#include "memo_container.h"
#include "memo_net.h"
#include "memo_spool.h"
#include "memo_state.h"
#include "memo_signal.h"

#include "esp_opus_enc.h"

static const char *TAG = "memo_recorder";

#define REC_SAMPLE_RATE 16000
#define REC_CHANNELS 1
#define REC_FRAME_MS 60
#define REC_FRAME_SAMPLES (REC_SAMPLE_RATE * REC_FRAME_MS / 1000) /* 960 */
#define REC_BATCH_BYTES 4096
/* esp_opus_enc 在编码路径上吃栈很凶：20 KB 会在第一帧附近就撞到保护页
   （实测 panic: Stack protection fault in task "memo_rec"）。参考项目给
   编解码任务的是 24 KB，这里留余量到 28 KB，并在停止时记录高水位。 */
#define REC_TASK_STACK 28672

typedef struct {
    atomic_bool active;
    esp_err_t error;
    bool write_failed;
    int64_t started_at;
    char id[37];
    void *file;
    uint32_t frames;
    uint64_t samples;
    atomic_uchar level, signal_hint;
    uint8_t batch[REC_BATCH_BYTES];
    size_t batch_len;
} recorder_state_t;

static recorder_state_t s_rec;
static SemaphoreHandle_t s_stopped;
static SemaphoreHandle_t s_start;
static TaskHandle_t s_task;
static void *s_encoder;
static int16_t *s_pcm;
static uint8_t *s_encoded;
static int s_in_frame_bytes;
static int s_out_frame_bytes;
static bool s_ready;
static bool s_prepared;

static uint32_t elapsed_ms(void)
{
    return (uint32_t)(s_rec.samples * 1000 / REC_SAMPLE_RATE);
}

static esp_err_t open_encoder(void)
{

    esp_opus_enc_config_t cfg = ESP_OPUS_ENC_CONFIG_DEFAULT();
    cfg.sample_rate = REC_SAMPLE_RATE;
    cfg.channel = ESP_AUDIO_MONO;
    cfg.bits_per_sample = ESP_AUDIO_BIT16;
    cfg.bitrate = 20000;
    cfg.frame_duration = ESP_OPUS_ENC_FRAME_DURATION_60_MS;
    cfg.application_mode = ESP_OPUS_ENC_APPLICATION_VOIP;
    cfg.complexity = 0;
    cfg.enable_fec = false;
    cfg.enable_dtx = false; /* 存档要每帧齐全，DTX 会吞掉静音帧 */
    cfg.enable_vbr = true;

    const uint32_t heap_before = esp_get_free_heap_size();
    esp_audio_err_t aerr = esp_opus_enc_open(&cfg, sizeof(cfg), &s_encoder);
    if (aerr != ESP_AUDIO_ERR_OK) {
        ESP_LOGE(TAG, "opus encoder open failed: %d", (int)aerr);
        s_encoder = NULL;
        return aerr == ESP_AUDIO_ERR_MEM_LACK ? ESP_ERR_NO_MEM : ESP_FAIL;
    }
    /* 编码器是这个应用最贵的一次性分配：记下它真实吃多少，才知道录音的余量够不够。 */
    ESP_LOGI(TAG, "opus encoder holds %u bytes (free heap %u)",
             (unsigned)(heap_before - esp_get_free_heap_size()),
             (unsigned)esp_get_free_heap_size());

    if (esp_opus_enc_get_frame_size(s_encoder, &s_in_frame_bytes, &s_out_frame_bytes) !=
            ESP_AUDIO_ERR_OK ||
        s_in_frame_bytes <= 0 || s_out_frame_bytes <= 0) {
        ESP_LOGE(TAG, "cannot query opus frame size");
        esp_opus_enc_close(s_encoder);
        s_encoder = NULL;
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t memo_recorder_init(void)
{
    if (s_ready) return ESP_OK;
    esp_err_t err = memo_spool_mount();
    if (err != ESP_OK) return err;
    s_stopped = xSemaphoreCreateBinary();
    if (s_stopped == NULL) return ESP_ERR_NO_MEM;
    s_start = xSemaphoreCreateBinary();
    if (s_start == NULL) {
        vSemaphoreDelete(s_stopped);
        s_stopped = NULL;
        return ESP_ERR_NO_MEM;
    }
    s_ready = true;
    return ESP_OK;
}

static bool flush_batch(void)
{
    if (s_rec.batch_len == 0) return true;
    if (memo_spool_append(s_rec.file, s_rec.batch, s_rec.batch_len) != ESP_OK) {
        s_rec.error = ESP_FAIL;
        s_rec.write_failed = true;
        ESP_LOGE(TAG, "spool append failed; capture stopped, complete frames retained");
        return false;
    }
    s_rec.batch_len = 0;
    return true;
}

static bool append_frame(const uint8_t *frame, size_t len)
{
    if (len == 0 || len + 2 > sizeof(s_rec.batch)) return false;
    if (s_rec.batch_len + 2 + len > sizeof(s_rec.batch) && !flush_batch()) return false;
    s_rec.batch[s_rec.batch_len++] = (uint8_t)(len & 0xFF);
    s_rec.batch[s_rec.batch_len++] = (uint8_t)(len >> 8);
    memcpy(s_rec.batch + s_rec.batch_len, frame, len);
    s_rec.batch_len += len;
    return true;
}

static void recorder_task(void *arg)
{
    (void)arg;
    /* Allocate the largest contiguous block (this stack) before the encoder.
     * The higher-priority worker must not touch resources until start commits. */
    xSemaphoreTake(s_start, portMAX_DELAY);
    int16_t *pcm = s_pcm;
    uint8_t *encoded = s_encoded;

    memo_signal_t signal = {0};
    /* The input heartbeat is sampled every 200 ms. Bound encoded frames too,
     * so the last interval can never exceed the server's five-minute cap. */
    while (s_rec.active && s_rec.frames < MEMO_RECORD_LIMIT_MS / REC_FRAME_MS) {
        size_t want = (size_t)REC_FRAME_SAMPLES * sizeof(int16_t);
        esp_err_t err = bsp_audio_read(pcm, want);
        if (err != ESP_OK) {
            s_rec.error = err;
            ESP_LOGE(TAG, "audio read failed: %s", esp_err_to_name(err));
            break;
        }

        const memo_signal_reading_t reading = memo_signal_read(&signal, pcm, REC_FRAME_SAMPLES, REC_FRAME_MS);
        s_rec.level = reading.level;
        s_rec.signal_hint = reading.hint;

        esp_audio_enc_in_frame_t in = {
            .buffer = (uint8_t *)pcm,
            .len = (uint32_t)want,
        };
        esp_audio_enc_out_frame_t out = {
            .buffer = encoded,
            .len = (uint32_t)s_out_frame_bytes,
            .encoded_bytes = 0,
        };
        if (esp_opus_enc_process(s_encoder, &in, &out) != ESP_AUDIO_ERR_OK || out.encoded_bytes == 0) {
            s_rec.error = ESP_FAIL;
            ESP_LOGE(TAG, "encode failed; capture stopped");
            break;
        }

        if (!append_frame(encoded, out.encoded_bytes)) {
            s_rec.error = ESP_FAIL;
            break;
        }
        s_rec.frames++;
        s_rec.samples += REC_FRAME_SAMPLES;

        if (s_rec.batch_len >= sizeof(s_rec.batch) - 2048) {
            /* 每 ~1.5 秒落一次盘，别让 flash 写和 I2S 抢同一个节拍。 */
            if (!flush_batch()) break;
        }
    }

    if (!s_rec.write_failed) flush_batch();
    s_rec.active = false;

    /* 记录栈高水位，下一次调栈大小就有依据，而不是再靠猜。 */
    UBaseType_t high_water = uxTaskGetStackHighWaterMark(NULL);
    ESP_LOGI(TAG, "recorder stack high-water: %u bytes free", (unsigned)(high_water * sizeof(StackType_t)));

    free(pcm);
    free(encoded);
    s_pcm = NULL;
    s_encoded = NULL;
    s_rec.level = 0;
    s_rec.signal_hint = MEMO_SIGNAL_NORMAL;
    xSemaphoreGive(s_stopped);
    vTaskSuspend(NULL); // Stop owner reclaims this large stack immediately.
}

esp_err_t memo_recorder_prepare(const char *id)
{
    if (!s_ready) return ESP_ERR_INVALID_STATE;
    if (id == NULL || strlen(id) != 36) return ESP_ERR_INVALID_ARG;
    if (s_rec.active || s_task != NULL) return ESP_ERR_INVALID_STATE;
    while (xSemaphoreTake(s_stopped, 0) == pdTRUE) {}
    while (xSemaphoreTake(s_start, 0) == pdTRUE) {}

    esp_err_t err = bsp_audio_set_format(REC_SAMPLE_RATE, 16, REC_CHANNELS);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "codec format: %s", esp_err_to_name(err));
        return err;
    }

    /* A 28 KB stack cannot be assembled from smaller free blocks. Creating it
     * after the 24.6 KB encoder failed even with 98 KB total heap available. */
    if (xTaskCreate(recorder_task, "memo_rec", REC_TASK_STACK, NULL, 6, &s_task) != pdPASS) {
        s_task = NULL;
        ESP_LOGE(TAG, "recorder task: out of memory (free heap %u)",
                 (unsigned)esp_get_free_heap_size());
        return ESP_ERR_NO_MEM;
    }
    err = open_encoder();
    if (err != ESP_OK) goto start_failed;
    s_pcm = malloc(REC_FRAME_SAMPLES * sizeof(int16_t));
    s_encoded = malloc((size_t)s_out_frame_bytes);
    if (s_pcm == NULL || s_encoded == NULL) {
        ESP_LOGE(TAG, "recorder buffers: out of memory");
        err = ESP_ERR_NO_MEM;
        goto start_failed;
    }

    /* 没连过网、也没 SNTP，time() 会是 1970 年起步的值。这种时间写进容器只会
       让列表显示一个假日期，所以按协议约定记 0（"从未对时"），由服务端兜底。 */
    const uint64_t recorded_at = memo_net_now(); /* 未对时则为 0，由服务端兜底 */

    memo_container_header_t header = {
        .sample_rate = REC_SAMPLE_RATE,
        .channels = REC_CHANNELS,
        .frame_ms = REC_FRAME_MS,
        .duration_ms = 0,
        .recorded_at = recorded_at,
        .frame_count = 0,
    };
    strncpy(header.memo_id, id, sizeof(header.memo_id) - 1);

    uint8_t raw[80];
    memo_container_encode(&header, raw);

    /* 每次开始都从头计数；文件句柄由本次创建。 */
    s_rec.frames = 0;
    s_rec.samples = 0;
    s_rec.batch_len = 0;
    s_rec.level = 0;
    s_rec.signal_hint = MEMO_SIGNAL_NORMAL;
    memset(s_rec.id, 0, sizeof(s_rec.id));
    strncpy(s_rec.id, id, sizeof(s_rec.id) - 1);
    s_rec.file = NULL;

    err = memo_spool_create(id, raw, &s_rec.file);
    if (err != ESP_OK) goto start_failed;
    s_rec.error = ESP_OK;
    s_rec.write_failed = false;
    s_prepared = true;
    return ESP_OK;

start_failed:
    /* Still parked at the start gate. Deleting from the owner releases its
     * stack immediately, before a possible network stand-down/retry. */
    vTaskDelete(s_task);
    s_task = NULL;
    if (s_encoder != NULL) esp_opus_enc_close(s_encoder);
    s_encoder = NULL;
    free(s_pcm); s_pcm = NULL;
    free(s_encoded); s_encoded = NULL;
    return err;
}

esp_err_t memo_recorder_begin(void)
{
    if (!s_prepared || s_task == NULL) return ESP_ERR_INVALID_STATE;
    s_prepared = false;
    s_rec.started_at = esp_timer_get_time();
    s_rec.active = true;
    xSemaphoreGive(s_start);
    ESP_LOGI(TAG, "recording %s", s_rec.id);
    return ESP_OK;
}

esp_err_t memo_recorder_cancel_prepared(void)
{
    if (!s_prepared) return ESP_ERR_INVALID_STATE;
    vTaskDelete(s_task); s_task = NULL;
    esp_opus_enc_close(s_encoder); s_encoder = NULL;
    free(s_pcm); s_pcm = NULL;
    free(s_encoded); s_encoded = NULL;
    const esp_err_t err = memo_spool_discard(s_rec.file, s_rec.id);
    s_rec.file = NULL;
    s_prepared = false;
    return err;
}

esp_err_t memo_recorder_start(const char *id)
{
    const esp_err_t err = memo_recorder_prepare(id);
    return err == ESP_OK ? memo_recorder_begin() : err;
}

esp_err_t memo_recorder_stop(bool keep, memo_recorder_result_t *result)
{
    if (!s_ready) return ESP_ERR_INVALID_STATE;
    if (s_prepared) return ESP_ERR_INVALID_STATE;
    if (!s_rec.active && s_task == NULL) return ESP_ERR_INVALID_STATE;

    s_rec.active = false;
    if (s_stopped != NULL) {
        if (xSemaphoreTake(s_stopped, pdMS_TO_TICKS(3000)) != pdTRUE) {
            ESP_LOGE(TAG, "recorder did not stop in time");
            return ESP_ERR_TIMEOUT;
        }
    }
    vTaskDelete(s_task);
    s_task = NULL;
    esp_opus_enc_close(s_encoder); s_encoder = NULL;

    uint32_t frames = s_rec.frames;
    uint32_t duration = elapsed_ms();

    if (!keep) {
        const esp_err_t err = memo_spool_discard(s_rec.file, s_rec.id);
        s_rec.file = NULL;
        if (err == ESP_OK) ESP_LOGI(TAG, "recording discarded");
        else ESP_LOGE(TAG, "recording discard failed: %s", esp_err_to_name(err));
        return err;
    }

    uint32_t size = 0;
    esp_err_t err = memo_spool_finalize(s_rec.file, s_rec.id, frames, duration, &size);
    s_rec.file = NULL;
    /* Re-scan packet boundaries after any short write/header failure. Recovery
     * leaves complete data in a draft; zero/invalid data remains quarantined. */
    if (err != ESP_OK || s_rec.error != ESP_OK || frames == 0) {
        memo_spool_recover();
        return err != ESP_OK ? err : ESP_FAIL;
    }

    if (result != NULL) {
        strncpy(result->id, s_rec.id, sizeof(result->id) - 1);
        result->id[sizeof(result->id) - 1] = '\0';
        result->duration_ms = duration;
        result->frame_count = frames;
        result->size_bytes = size;
    }
    return ESP_OK;
}

uint8_t memo_recorder_level(void)
{
    return s_rec.active ? s_rec.level : 0;
}

uint32_t memo_recorder_elapsed_ms(void)
{
    return s_task != NULL && !s_prepared ? (uint32_t)((esp_timer_get_time() - s_rec.started_at) / 1000) : 0;
}

bool memo_recorder_is_active(void)
{
    return s_rec.active;
}

bool memo_recorder_is_busy(void)
{
    return s_task != NULL;
}

uint8_t memo_recorder_signal_hint(void)
{
    return s_rec.signal_hint;
}
