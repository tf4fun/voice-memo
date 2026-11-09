// main/memo_ui_common.h —— 视觉设计语言里的基础件与配色。
//
// 只放"每个页面都会用到"的东西：颜色、卡片、标签。页面自己的布局留在各页面文件里。
#pragma once

#include "lvgl.h"

/* 配色：磷光绿 CRT 终端（docs/design/visual-language.zh_CN.md §1）。
   严格单色绿；状态靠亮度、字形与反白表达，不靠色相。 */
#define TERM_BG    0x061006  /* 屏幕/面板底色，也是反白文字的颜色 */
#define TERM_FG    0x8CEC66  /* 暖绿磷光：文字、线条、选中填充 */
#define TERM_DIM   0x4F8D3B  /* 次要文字、占位 */
#define TERM_DARK  0x1D3B20  /* 静止边框、未点亮的电平、滚动条轨道 */

/* 形状与线宽：方角、1 px 线、无投影 */
#define VM_RADIUS_CARD   0
#define VM_RADIUS_PANEL  0
#define VM_STROKE        1
#define VM_INSET         8

/* 去掉默认样式，只留一个纯色圆角矩形。 */
void memo_ui_flat_rect(lv_obj_t *obj, uint32_t color, int radius);

/* 面板 = 底色方块 + 1 px 暗绿边框。无投影；shadow_out 恒为 NULL，仅为兼容调用方。 */
lv_obj_t *memo_ui_card(lv_obj_t *parent, int x, int y, int w, int h, lv_obj_t **shadow_out);

/* 建一个指定字体与颜色的标签。 */
lv_obj_t *memo_ui_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color);

/* 显示/隐藏一个面板（shadow 为 NULL 时只处理面板）。 */
void memo_ui_card_visible(lv_obj_t *card, lv_obj_t *shadow, bool visible);

/* Live pages register battery labels; cached SOC is sampled by the input
 * owner outside the LVGL lock. Creation/update require that lock. */
lv_obj_t *memo_ui_battery(lv_obj_t *screen);
void memo_ui_battery_update(int soc);
/* Unregister labels and delete an inactive screen and its children/animations.
 * Hold the LVGL lock; switch to another screen before calling. */
void memo_ui_screen_delete(lv_obj_t *screen);

/* Single low-brightness terminal rule, without an emphasized leading segment. */
void memo_ui_rule(lv_obj_t *screen, int x, int y, int w);

/* 1 px framed track with a centred, 1 px inset fill; hold LVGL lock. */
lv_obj_t *memo_ui_meter_create(lv_obj_t *parent, int width, int height, lv_obj_t **fill);

/* Shared Chinese title, cached battery and rule. Returns the title label. */
lv_obj_t *memo_ui_header(lv_obj_t *screen, const char *title);

/* 反白：selected 时背景填亮绿，所有 text 对象改为底色；否则恢复。 */
void memo_ui_inverse(lv_obj_t *box, bool selected);

/* 4 px 轨道 + 亮绿滑块的位置指示器；创建与更新都需持 LVGL 锁。 */
lv_obj_t *memo_ui_scrollbar_create(lv_obj_t *screen, int x, int y, int h);
void memo_ui_scrollbar_update(lv_obj_t *track, int track_h, int total, int visible, int top);
