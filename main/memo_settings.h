// main/memo_settings.h —— 设备设置的持久化（NVS）。
//
// 目前真正被使用的：音量（播放立刻生效）、时区（列表/详情时间显示）、
// 自动播放（进详情页即播）。息屏与休眠时间先存下来，等 M5 的电源管理接上。
#pragma once

#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint8_t volume;           /* 0..100，步进 20 */
    uint8_t screen_timeout_s; /* 0 = 常亮 */
    uint8_t sleep_min;        /* 0 = 不自动休眠 */
    bool autoplay;
    int16_t tz_offset_min;
} memo_settings_t;

/* 初始化 NVS 并载入设置；未写入过的项用默认值。 */
esp_err_t memo_settings_init(void);

/* 当前设置（只读）。未初始化时返回默认值。 */
const memo_settings_t *memo_settings_get(void);

/* 逐项修改并持久化；音量会立刻应用到 codec。 */
esp_err_t memo_settings_set_volume(uint8_t percent);
esp_err_t memo_settings_set_screen_timeout(uint8_t seconds);
esp_err_t memo_settings_set_sleep(uint8_t minutes);
esp_err_t memo_settings_set_autoplay(bool enabled);
esp_err_t memo_settings_set_tz(int16_t offset_min);

/* 选项表的访问器，供设置页循环切换。 */
uint8_t memo_settings_volume_step(uint8_t current, int direction);
uint8_t memo_settings_timeout_step(uint8_t current, int direction);
uint8_t memo_settings_sleep_step(uint8_t current, int direction);
