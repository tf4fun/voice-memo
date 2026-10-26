// main/memo_ui_detail.h —— 详情页：正文折叠/展开 + 播放条。
#pragma once

#include "lvgl.h"

#include <stdbool.h>
#include <stdint.h>

/* 建好页面（默认隐藏）。需持 LVGL 锁调用。 */
void memo_ui_detail_build(void);

/* 载入一条记录：时间、时长与占位文字。需持 LVGL 锁调用。 */
void memo_ui_detail_show(int64_t recorded_at, uint32_t duration_ms, const char *transcript);

/* 折叠 / 展开。需持 LVGL 锁调用。 */
void memo_ui_detail_set_expanded(bool expanded);

/* 刷新播放条（位置、总时长、播放/暂停图标）。需持 LVGL 锁调用。 */
void memo_ui_detail_set_playback(bool playing, uint32_t position_ms, uint32_t duration_ms);

/* 页面对象，供 lv_screen_load 使用。 */
lv_obj_t *memo_ui_detail_screen(void);
