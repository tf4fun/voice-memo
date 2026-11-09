// main/memo_font.h —— 中文字体入口。
//
// 两个子集由 tools/generate_memo_fonts.py 生成，覆盖 GB2312 一级汉字 + 常用标点 + ASCII：
//   memo_font_20  正文（常规），20 px
//   memo_font_16  列表/标签（粗体），16 px
// 两者都挂 Montserrat 作为 fallback，用来补图标与字形更合适的拉丁字符。
#pragma once

#include "lvgl.h"

LV_FONT_DECLARE(memo_font_16);
LV_FONT_DECLARE(memo_font_20);
LV_FONT_DECLARE(memo_font_terminal);

/* 建立带 fallback 的可写描述符。必须在创建任何控件之前、持 LVGL 锁调用一次。 */
void memo_fonts_init(void);

/* 正文（20 px 常规）；未初始化时退回 Montserrat 20。 */
const lv_font_t *memo_font_body(void);

/* 标签（16 px 粗体）；未初始化时退回 Montserrat 14。 */
const lv_font_t *memo_font_label(void);

/* 等宽像素字体（8 px 字宽、18 px 行高，仅 ASCII）：时间、时长、电量等终端文本。
   不得用于含汉字的文本。 */
const lv_font_t *memo_font_mono(void);

/* 该字体是否覆盖某个码点（用于覆盖率自检，缺字要能被发现而不是画成空白）。 */
bool memo_font_covers(uint32_t codepoint);

/* 逐码点检查并在日志里报告缺字，例如 "missing glyph U+25B6"。
   缺字在屏幕上会变成方框，光看界面很难定位是哪个字符，所以在这里报出来。 */
void memo_font_check_text(const char *what, const char *text);

/* Dynamic text: preserve supported UTF-8, replace unsupported characters with
 * '?'. Static UI literals are separately checked against both font cmaps.
 * Caller holds LVGL lock. Returns false on allocation failure (error label shown). */
bool memo_font_label_text(lv_obj_t *label, const char *text, bool body);
