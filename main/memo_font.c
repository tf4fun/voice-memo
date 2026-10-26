// main/memo_font.c —— 字体描述符与覆盖率检查。
#include "memo_font.h"

static lv_font_t s_body;
static lv_font_t s_label;
static bool s_ready;

void memo_fonts_init(void)
{
    if (s_ready) return;

    /* 复制描述符后挂 fallback：字体数据本身是只读的，这里只扩展查找链。 */
    s_body = memo_font_20;
    s_body.fallback = &lv_font_montserrat_20;

    s_label = memo_font_16;
    s_label.fallback = &lv_font_montserrat_14;

    s_ready = true;
}

const lv_font_t *memo_font_body(void)
{
    return s_ready ? &s_body : &lv_font_montserrat_20;
}

const lv_font_t *memo_font_label(void)
{
    return s_ready ? &s_label : &lv_font_montserrat_14;
}

bool memo_font_covers(uint32_t codepoint)
{
    lv_font_glyph_dsc_t glyph = {0};
    const lv_font_t *font = memo_font_label();
    if (!lv_font_get_glyph_dsc(font, &glyph, codepoint, 0)) return false;
    return !glyph.is_placeholder;
}
