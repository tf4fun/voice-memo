#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef int esp_audio_err_t;
#define ESP_AUDIO_ERR_OK 0
#define ESP_OPUS_DEC_FRAME_DURATION_INVALID 0
typedef struct { int sample_rate, channel, frame_duration; bool self_delimited; } esp_opus_dec_cfg_t;
typedef struct { uint8_t *buffer; unsigned len, consumed; } esp_audio_dec_in_raw_t;
typedef struct { uint8_t *buffer; unsigned len, decoded_size; } esp_audio_dec_out_frame_t;
typedef struct { int unused; } esp_audio_dec_info_t;
esp_audio_err_t esp_opus_dec_open(void *cfg, int len, void **dec);
esp_audio_err_t esp_opus_dec_decode(void *dec, esp_audio_dec_in_raw_t *in, esp_audio_dec_out_frame_t *out, esp_audio_dec_info_t *info);
void esp_opus_dec_close(void *dec);
