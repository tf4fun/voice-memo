// main/memo_recorder.c —— Opus 录音任务。
//
// 参数与 docs/technical/client.md §7 一致：16 kHz 单声道、60 ms 帧、20 kbps、
// complexity 0、DTX 关闭（录音存档要求每帧齐全）。
#include "memo_recorder.h"

#include <stdlib.h>
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
    bool active;
    char id[37];
    void *file;
    uint32_t frames;
    uint64_t samples;
    uint8_t level;
    uint8_t batch[REC_BATCH_BYTES];
    size_t batch_len;
} recorder_state_t;

static recorder_state_t s_rec;
static SemaphoreHandle_t s_stopped;
static TaskHandle_t s_task;
static void *s_encoder;
static int s_in_frame_bytes;
static int s_out_frame_bytes;
static bool s_ready;

static uint32_t elapsed_ms(void)
{
    return (uint32_t)(s_rec.samples * 1000 / REC_SAMPLE_RATE);
}

esp_err_t memo_recorder_init(void)
{
    if (s_ready) return ESP_OK;

    esp_err_t err = memo_spool_mount();
    if (err != ESP_OK) return err;

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
        return ESP_FAIL;
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

    s_stopped = xSemaphoreCreateBinary();
    if (s_stopped == NULL) {
        esp_opus_enc_close(s_encoder);
        s_encoder = NULL;
        return ESP_ERR_NO_MEM;
    }

    s_ready = true;
    ESP_LOGI(TAG, "ready: %d in / %d out bytes per %d ms frame",
             s_in_frame_bytes, s_out_frame_bytes, REC_FRAME_MS);
    return ESP_OK;
}

static void flush_batch(void)
{
    if (s_rec.batch_len == 0) return;
    if (memo_spool_append(s_rec.file, s_rec.batch, s_rec.batch_len) != ESP_OK) {
        ESP_LOGE(TAG, "spool append failed, dropping batch");
    }
    s_rec.batch_len = 0;
}

static void append_frame(const uint8_t *frame, size_t len)
{
    if (len > 0xFFFF) return;
    if (s_rec.batch_len + 2 + len > sizeof(s_rec.batch)) {
        flush_batch();
    }
    s_rec.batch[s_rec.batch_len++] = (uint8_t)(len & 0xFF);
    s_rec.batch[s_rec.batch_len++] = (uint8_t)(len >> 8);
    memcpy(s_rec.batch + s_rec.batch_len, frame, len);
    s_rec.batch_len += len;
}

static uint8_t level_from_pcm(const int16_t *pcm, size_t samples)
{
    if (samples == 0) return 0;

    int64_t sum = 0;
    for (size_t i = 0; i < samples; i++) {
        sum += (int64_t)pcm[i] * pcm[i];
    }
    int64_t rms = 0;
    /* 整数平方根，避免引入 math.h 的浮点。 */
    int64_t value = sum / (int64_t)samples;
    int64_t root = 0, bit = (int64_t)1 << 30;
    while (bit > value) bit >>= 2;
    while (bit != 0) {
        if (value >= root + bit) {
            value -= root + bit;
            root = (root >> 1) + bit;
        } else {
            root >>= 1;
        }
        bit >>= 2;
    }
    /* 3000 的 RMS 大致对应满格，按比例压到 0..100。 */
    rms = root;
    int level = (int)(rms * 100 / 3000);
    if (level > 100) level = 100;
    return (uint8_t)level;
}

static void recorder_task(void *arg)
{
    (void)arg;

    int16_t *pcm = malloc(REC_FRAME_SAMPLES * sizeof(int16_t));
    uint8_t *encoded = malloc((size_t)s_out_frame_bytes);
    if (pcm == NULL || encoded == NULL) {
        ESP_LOGE(TAG, "recorder buffers: out of memory");
        free(pcm);
        free(encoded);
        s_rec.active = false;
        xSemaphoreGive(s_stopped);
        vTaskDelete(NULL);
        return;
    }

    while (s_rec.active) {
        size_t want = (size_t)REC_FRAME_SAMPLES * sizeof(int16_t);
        esp_err_t err = bsp_audio_read(pcm, want);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "audio read failed: %s", esp_err_to_name(err));
            break;
        }

        s_rec.level = level_from_pcm(pcm, REC_FRAME_SAMPLES);

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
            ESP_LOGW(TAG, "encode failed, skipping frame");
            continue;
        }

        append_frame(encoded, out.encoded_bytes);
        s_rec.frames++;
        s_rec.samples += REC_FRAME_SAMPLES;

        if (s_rec.batch_len >= sizeof(s_rec.batch) - 2048) {
            /* 每 ~1.5 秒落一次盘，别让 flash 写和 I2S 抢同一个节拍。 */
            flush_batch();
        }
    }

    flush_batch();

    /* 记录栈高水位，下一次调栈大小就有依据，而不是再靠猜。 */
    UBaseType_t high_water = uxTaskGetStackHighWaterMark(NULL);
    ESP_LOGI(TAG, "recorder stack high-water: %u bytes free", (unsigned)(high_water * sizeof(StackType_t)));

    free(pcm);
    free(encoded);
    s_rec.level = 0;
    xSemaphoreGive(s_stopped);
    vTaskDelete(NULL);
}

esp_err_t memo_recorder_start(const char *id)
{
    if (!s_ready) return ESP_ERR_INVALID_STATE;
    if (id == NULL || strlen(id) != 36) return ESP_ERR_INVALID_ARG;
    if (s_rec.active) return ESP_ERR_INVALID_STATE;

    esp_err_t err = bsp_audio_set_format(REC_SAMPLE_RATE, 16, REC_CHANNELS);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "codec format: %s", esp_err_to_name(err));
        return err;
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
    memset(s_rec.id, 0, sizeof(s_rec.id));
    strncpy(s_rec.id, id, sizeof(s_rec.id) - 1);
    s_rec.file = NULL;

    err = memo_spool_create(id, raw, &s_rec.file);
    if (err != ESP_OK) return err;

    if (s_stopped == NULL) {
        s_stopped = xSemaphoreCreateBinary();
    }
    s_rec.active = true;

    if (xTaskCreate(recorder_task, "memo_rec", REC_TASK_STACK, NULL, 6, &s_task) != pdPASS) {
        s_rec.active = false;
        memo_spool_discard(s_rec.file, id);
        s_rec.file = NULL;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "recording %s", id);
    return ESP_OK;
}

esp_err_t memo_recorder_stop(bool keep, memo_recorder_result_t *result)
{
    if (!s_ready) return ESP_ERR_INVALID_STATE;
    if (!s_rec.active && s_task == NULL) return ESP_ERR_INVALID_STATE;

    s_rec.active = false;
    if (s_stopped != NULL) {
        if (xSemaphoreTake(s_stopped, pdMS_TO_TICKS(3000)) != pdTRUE) {
            ESP_LOGE(TAG, "recorder did not stop in time");
            return ESP_ERR_TIMEOUT;
        }
    }
    s_task = NULL;

    uint32_t frames = s_rec.frames;
    uint32_t duration = elapsed_ms();

    if (!keep) {
        memo_spool_discard(s_rec.file, s_rec.id);
        s_rec.file = NULL;
        ESP_LOGI(TAG, "recording discarded");
        return ESP_OK;
    }

    uint32_t size = 0;
    esp_err_t err = memo_spool_finalize(s_rec.file, frames, duration, &size);
    s_rec.file = NULL;
    if (err != ESP_OK) return err;

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
    return s_rec.active ? elapsed_ms() : 0;
}

bool memo_recorder_is_active(void)
{
    return s_rec.active;
}
