// main/memo_ui_detail.h —— 详情页：完整正文与自动滚屏 + 播放条。
#pragma once

#include "lvgl.h"
#include "memo_state.h"

#include <stdbool.h>
#include <stdint.h>

/* 建好页面（默认隐藏）。需持 LVGL 锁调用。 */
void memo_ui_detail_build(void);
/* Delete after switching away; hold LVGL lock. */
void memo_ui_detail_destroy(void);

/* 载入一条记录：时间、时长与占位文字。需持 LVGL 锁调用。 */
void memo_ui_detail_show(int64_t recorded_at, uint32_t duration_ms, const char *transcript);

/* 刷新播放条（位置、总时长、播放/暂停图标）。需持 LVGL 锁调用。 */
void memo_ui_detail_set_playback(bool playing, uint32_t position_ms, uint32_t duration_ms);

/* 页面对象，供 lv_screen_load 使用。 */
lv_obj_t *memo_ui_detail_screen(void);

bool memo_ui_detail_text(const char *text, bool bottom);
void memo_ui_detail_mode(memo_state_t state, bool pending, bool error);
/* Follow a proportional byte target inside the rendered window; hold LVGL lock. */
void memo_ui_detail_follow(uint32_t target, uint32_t offset, uint32_t end);

void memo_ui_detail_status(bool local, const char *status);
/* Temporarily replace status/volume with feedback; the next mode render restores it. */
void memo_ui_detail_message(const char *message);
