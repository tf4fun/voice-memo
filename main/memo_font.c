// main/memo_font.c —— 字体描述符与覆盖率检查。
#include "memo_font.h"

#include "esp_log.h"

static const char *TAG = "memo_font";

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

void memo_font_check_text(const char *what, const char *text)
{
    if (text == NULL) return;

    const unsigned char *p = (const unsigned char *)text;
    while (*p != '\0') {
        uint32_t cp;
        int extra;
        if (*p < 0x80) {
            cp = *p;
            extra = 0;
        } else if ((*p & 0xE0) == 0xC0) {
            cp = *p & 0x1F;
            extra = 1;
        } else if ((*p & 0xF0) == 0xE0) {
            cp = *p & 0x0F;
            extra = 2;
        } else if ((*p & 0xF8) == 0xF0) {
            cp = *p & 0x07;
            extra = 3;
        } else {
            p++;
            continue;
        }
        for (int i = 1; i <= extra; i++) {
            if ((p[i] & 0xC0) != 0x80) {
                extra = i - 1;
                break;
            }
            cp = (cp << 6) | (p[i] & 0x3F);
        }
        p += extra + 1;

        if (cp >= 0x20 && !memo_font_covers(cp)) {
            ESP_LOGW(TAG, "%s: missing glyph U+%04X", what, (unsigned)cp);
        }
    }
}
