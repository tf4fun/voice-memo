// main/memo_settings.c
#include "memo_settings.h"

#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "bsp_audio.h"

static const char *TAG = "memo_settings";
static const char *NS = "memo";

static memo_settings_t s_settings;
static bool s_ready;
static nvs_handle_t s_nvs;

/* 音量 20% 一档：从 20 开始，0 表示静音，暂停在 100。 */
static const uint8_t kVolumeSteps[] = {0, 20, 40, 60, 80, 100};
static const uint8_t kTimeoutSteps[] = {0, 30, 60, 180};
static const uint8_t kSleepSteps[] = {0, 1, 5, 15};

static const memo_settings_t kDefaults = {
    .volume = 80,
    .screen_timeout_s = 30,
    .sleep_min = 5,
    .autoplay = false,
    .tz_offset_min = 480,
};

static uint8_t step_in(const uint8_t *table, size_t count, uint8_t current, int direction)
{
    size_t index = 0;
    for (size_t i = 0; i < count; i++) {
        if (table[i] == current) {
            index = i;
            break;
        }
    }
    if (direction > 0) {
        index = (index + 1) % count;
    } else if (direction < 0) {
        index = (index + count - 1) % count;
    }
    return table[index];
}

uint8_t memo_settings_volume_step(uint8_t current, int direction)
{
    return step_in(kVolumeSteps, sizeof(kVolumeSteps), current, direction);
}

uint8_t memo_settings_timeout_step(uint8_t current, int direction)
{
    return step_in(kTimeoutSteps, sizeof(kTimeoutSteps), current, direction);
}

uint8_t memo_settings_sleep_step(uint8_t current, int direction)
{
    return step_in(kSleepSteps, sizeof(kSleepSteps), current, direction);
}

static int32_t read_i32(const char *key, int32_t fallback)
{
    int32_t value = fallback;
    if (nvs_get_i32(s_nvs, key, &value) != ESP_OK) return fallback;
    return value;
}

esp_err_t memo_settings_init(void)
{
    if (s_ready) return ESP_OK;

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        /* 分区被旧固件写过或已满：擦掉重来。这里只有设置，没有用户数据。 */
        ESP_LOGW(TAG, "NVS needs a reset: %s", esp_err_to_name(err));
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    if (err != ESP_OK) return err;

    if (nvs_open(NS, NVS_READWRITE, &s_nvs) != ESP_OK) {
        s_nvs = 0;
    }

    s_settings = kDefaults;
    if (s_nvs != 0) {
        s_settings.volume = (uint8_t)read_i32("vol", kDefaults.volume);
        s_settings.screen_timeout_s = (uint8_t)read_i32("to", kDefaults.screen_timeout_s);
        s_settings.sleep_min = (uint8_t)read_i32("sl", kDefaults.sleep_min);
        s_settings.autoplay = read_i32("ap", kDefaults.autoplay ? 1 : 0) != 0;
        s_settings.tz_offset_min = (int16_t)read_i32("tz", kDefaults.tz_offset_min);
    }
    if (s_settings.volume > 100) s_settings.volume = kDefaults.volume;

    s_ready = true;
    ESP_LOGI(TAG, "settings: volume=%u%% timeout=%us sleep=%umin autoplay=%d tz=%d",
             s_settings.volume, s_settings.screen_timeout_s, s_settings.sleep_min,
             s_settings.autoplay, s_settings.tz_offset_min);
    return ESP_OK;
}

const memo_settings_t *memo_settings_get(void)
{
    return s_ready ? &s_settings : &kDefaults;
}

static void persist_i32(const char *key, int32_t value)
{
    if (s_nvs == 0) return;
    nvs_set_i32(s_nvs, key, value);
    nvs_commit(s_nvs);
}

esp_err_t memo_settings_set_volume(uint8_t percent)
{
    if (percent > 100) return ESP_ERR_INVALID_ARG;
    s_settings.volume = percent;
    persist_i32("vol", percent);
    bsp_audio_set_volume(percent);
    return ESP_OK;
}

esp_err_t memo_settings_set_screen_timeout(uint8_t seconds)
{
    s_settings.screen_timeout_s = seconds;
    persist_i32("to", seconds);
    return ESP_OK;
}

esp_err_t memo_settings_set_sleep(uint8_t minutes)
{
    s_settings.sleep_min = minutes;
    persist_i32("sl", minutes);
    return ESP_OK;
}

esp_err_t memo_settings_set_autoplay(bool enabled)
{
    s_settings.autoplay = enabled;
    persist_i32("ap", enabled ? 1 : 0);
    return ESP_OK;
}

esp_err_t memo_settings_set_tz(int16_t offset_min)
{
    s_settings.tz_offset_min = offset_min;
    persist_i32("tz", offset_min);
    return ESP_OK;
}
