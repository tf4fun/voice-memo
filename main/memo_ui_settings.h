// main/memo_ui_settings.h —— 设置页与子页。
#pragma once

#include "lvgl.h"

#include <stdbool.h>

/* 建页面（默认隐藏）。需持 LVGL 锁调用。 */
void memo_ui_settings_build(void);

/* 每次进入设置页时刷新一次显示值。需持 LVGL 锁调用。 */
void memo_ui_settings_refresh(void);

/* 选中项移动，返回当前行号。需持 LVGL 锁调用。 */
int memo_ui_settings_move(int direction);

/* 当前行号。 */
int memo_ui_settings_selected(void);

/* 当前行是否可进入子页（音量/息屏/休眠）或可切换（自动播放）。 */
bool memo_ui_settings_row_editable(int row);

/* 进入当前行的子页 / 就地切换自动播放。需持 LVGL 锁调用。 */
void memo_ui_settings_enter(void);

/* 子页里改值（direction ±1），立即生效。需持 LVGL 锁调用。 */
void memo_ui_settings_adjust(int direction);

/* 当前是否停在子页（音量/息屏/休眠）。自动播放是就地切换，不算子页。 */
bool memo_ui_settings_in_sheet(void);

lv_obj_t *memo_ui_settings_screen(void);
