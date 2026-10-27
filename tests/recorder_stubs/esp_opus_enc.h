#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef int esp_audio_err_t;
#define ESP_AUDIO_ERR_OK 0
#define ESP_AUDIO_ERR_MEM_LACK (-2)
#define ESP_AUDIO_MONO 1
#define ESP_AUDIO_BIT16 16
#define ESP_OPUS_ENC_FRAME_DURATION_60_MS 60
#define ESP_OPUS_ENC_APPLICATION_VOIP 1
typedef struct {
    int sample_rate, channel, bits_per_sample, bitrate, frame_duration, application_mode, complexity;
    bool enable_fec, enable_dtx, enable_vbr;
} esp_opus_enc_config_t;
#define ESP_OPUS_ENC_CONFIG_DEFAULT() {0}
typedef struct { uint8_t *buffer; uint32_t len; } esp_audio_enc_in_frame_t;
typedef struct { uint8_t *buffer; uint32_t len, encoded_bytes; } esp_audio_enc_out_frame_t;
esp_audio_err_t esp_opus_enc_open(void *cfg, int len, void **encoder);
esp_audio_err_t esp_opus_enc_get_frame_size(void *enc, int *input, int *output);
esp_audio_err_t esp_opus_enc_process(void *enc, esp_audio_enc_in_frame_t *in, esp_audio_enc_out_frame_t *out);
void esp_opus_enc_close(void *enc);
uint32_t esp_get_free_heap_size(void);
