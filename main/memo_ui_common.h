// main/memo_ui_common.h —— 视觉设计语言里的基础件与配色。
//
// 只放"每个页面都会用到"的东西：颜色、卡片、标签。页面自己的布局留在各页面文件里。
#pragma once

#include "lvgl.h"

/* 配色（docs/design/visual-language.zh_CN.md §3） */
#define VM_PAPER  0xF6F1E6
#define VM_CARD   0xFFFFFF
#define VM_INK    0x141210
#define VM_MUTED  0x7A7368
#define VM_SUN    0xFFC53D
#define VM_CORAL  0xE5483F

/* 形状与线宽 */
#define VM_RADIUS_CARD   12
#define VM_RADIUS_PANEL  14
#define VM_STROKE        2
#define VM_SHADOW        3
#define VM_INSET         8

/* 去掉默认样式，只留一个纯色圆角矩形。 */
void memo_ui_flat_rect(lv_obj_t *obj, uint32_t color, int radius);

/* 卡片 = 实心墨块投影 + 白底墨线圆角矩形。投影对象通过 shadow_out 带回，
   因为隐藏/移动一张卡片必须同时处理两个对象。 */
lv_obj_t *memo_ui_card(lv_obj_t *parent, int x, int y, int w, int h, lv_obj_t **shadow_out);

/* 建一个指定字体与颜色的标签。 */
lv_obj_t *memo_ui_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color);

/* 同时显示/隐藏一张卡片和它的投影。 */
void memo_ui_card_visible(lv_obj_t *card, lv_obj_t *shadow, bool visible);
