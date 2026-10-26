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

/* 行的语义编号，外壳据此决定进入配网页还是子页。 */
typedef enum {
    MEMO_SETTING_NETWORK = 0,
    MEMO_SETTING_SERVER,
    MEMO_SETTING_VOLUME,
    MEMO_SETTING_TIMEOUT,
    MEMO_SETTING_SLEEP,
    MEMO_SETTING_AUTOPLAY,
    MEMO_SETTING_ABOUT,
} memo_setting_row_t;

memo_setting_row_t memo_ui_settings_row(void);

/* 当前行是否可进入子页（音量/息屏/休眠）或可切换（自动播放）。 */
bool memo_ui_settings_row_editable(int row);

/* 进入当前行的子页 / 就地切换自动播放。需持 LVGL 锁调用。 */
void memo_ui_settings_enter(void);

/* 子页里改值（direction ±1），立即生效。需持 LVGL 锁调用。 */
void memo_ui_settings_adjust(int direction);

/* 当前是否停在子页（音量/息屏/休眠）。自动播放是就地切换，不算子页。 */
bool memo_ui_settings_in_sheet(void);

lv_obj_t *memo_ui_settings_screen(void);
