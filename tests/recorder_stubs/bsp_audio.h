#pragma once
#include <stddef.h>
#include "esp_err.h"
esp_err_t bsp_audio_set_format(int rate, int bits, int channels);
esp_err_t bsp_audio_read(void *data, size_t bytes);
