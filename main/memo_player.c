// main/memo_player.c —— 把 spool 里的容器解码回 PCM 并从 codec 播出。
#include "memo_player.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "esp_log.h"

#include "bsp_audio.h"
#include "memo_container.h"
#include "memo_spool.h"

#include "esp_opus_dec.h"

static const char *TAG = "memo_player";

#define PLAYER_STACK 16384
#define PLAYER_PCM_BYTES 8192 /* 支持到 120 ms 的帧，留足余量 */

static void *s_decoder;
static TaskHandle_t s_task;
static SemaphoreHandle_t s_done;
static volatile bool s_active;
static char s_id[37];
static bool s_ready;
static volatile uint32_t s_position_ms;
static volatile uint32_t s_duration_ms;

esp_err_t memo_player_init(void)
{
    if (s_ready) return ESP_OK;

    esp_opus_dec_cfg_t cfg = {
        .sample_rate = 16000,
        .channel = 1,
        .frame_duration = ESP_OPUS_DEC_FRAME_DURATION_INVALID,
        .self_delimited = false,
    };
    esp_audio_err_t err = esp_opus_dec_open(&cfg, sizeof(cfg), &s_decoder);
    if (err != ESP_AUDIO_ERR_OK) {
        ESP_LOGE(TAG, "opus decoder open failed: %d", (int)err);
        s_decoder = NULL;
        return ESP_FAIL;
    }
    s_done = xSemaphoreCreateBinary();
    if (s_done == NULL) {
        esp_opus_dec_close(s_decoder);
        s_decoder = NULL;
        return ESP_ERR_NO_MEM;
    }
    s_ready = true;
    return ESP_OK;
}

static void player_task(void *arg)
{
    (void)arg;

    char path[64];
    snprintf(path, sizeof(path), "%s/%s.memo", MEMO_SPOOL_MOUNT_POINT, s_id);

    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        ESP_LOGE(TAG, "cannot open %s", path);
        goto done;
    }

    uint8_t raw[MEMO_CONTAINER_HEADER_SIZE];
    if (fread(raw, 1, sizeof(raw), file) != sizeof(raw)) {
        ESP_LOGE(TAG, "short header in %s", path);
        fclose(file);
        goto done;
    }

    memo_container_header_t header;
    if (!memo_container_decode(raw, &header)) {
        ESP_LOGE(TAG, "bad container in %s", path);
        fclose(file);
        goto done;
    }

    s_duration_ms = header.duration_ms;
    s_position_ms = 0;

    if (bsp_audio_set_format(header.sample_rate, 16, header.channels) != ESP_OK) {
        ESP_LOGE(TAG, "codec format for playback failed");
        fclose(file);
        goto done;
    }

    uint8_t *pcm = malloc(PLAYER_PCM_BYTES);
    uint8_t *packet = malloc(2048);
    if (pcm == NULL || packet == NULL) {
        ESP_LOGE(TAG, "player buffers: out of memory");
        free(pcm);
        free(packet);
        fclose(file);
        goto done;
    }

    uint32_t played = 0;
    while (s_active) {
        uint8_t length[2];
        if (fread(length, 1, 2, file) != 2) break;
        uint16_t size = (uint16_t)(length[0] | (length[1] << 8));
        if (size == 0 || size > 2048) break;
        if (fread(packet, 1, size, file) != size) break;

        esp_audio_dec_in_raw_t in = {.buffer = packet, .len = size, .consumed = 0};
        esp_audio_dec_out_frame_t out = {.buffer = pcm, .len = PLAYER_PCM_BYTES};
        esp_audio_dec_info_t info = {0};
        if (esp_opus_dec_decode(s_decoder, &in, &out, &info) != ESP_AUDIO_ERR_OK) {
            ESP_LOGW(TAG, "decode failed at frame %u", (unsigned)played);
            break;
        }
        if (out.decoded_size == 0) continue;
        if (bsp_audio_write(pcm, out.decoded_size) != ESP_OK) {
            ESP_LOGE(TAG, "playback write failed");
            break;
        }
        played++;
        /* 16 bit 单声道：字节数 / (采样率 × 2) → 毫秒 */
        s_position_ms = (uint32_t)(played * header.frame_ms);
    }

    ESP_LOGI(TAG, "played %u frames", (unsigned)played);
    free(pcm);
    free(packet);
    fclose(file);

done:
    s_position_ms = 0;
    s_active = false;
    if (s_done != NULL) xSemaphoreGive(s_done);
    s_task = NULL;
    vTaskDelete(NULL);
}

esp_err_t memo_player_play(const char *id)
{
    if (!s_ready) return ESP_ERR_INVALID_STATE;
    if (id == NULL || strlen(id) != 36) return ESP_ERR_INVALID_ARG;
    if (s_active) return ESP_ERR_INVALID_STATE;

    strncpy(s_id, id, sizeof(s_id) - 1);
    s_id[sizeof(s_id) - 1] = '\0';
    s_active = true;

    if (xTaskCreate(player_task, "memo_play", PLAYER_STACK, NULL, 5, &s_task) != pdPASS) {
        s_active = false;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t memo_player_stop(void)
{
    if (!s_active && s_task == NULL) return ESP_OK;

    s_active = false;
    if (s_done != NULL) {
        if (xSemaphoreTake(s_done, pdMS_TO_TICKS(3000)) != pdTRUE) {
            ESP_LOGE(TAG, "player did not stop in time");
            return ESP_ERR_TIMEOUT;
        }
    }
    return ESP_OK;
}

bool memo_player_is_active(void)
{
    return s_active;
}

uint32_t memo_player_position_ms(void)
{
    return s_position_ms;
}

uint32_t memo_player_duration_ms(void)
{
    return s_duration_ms;
}
