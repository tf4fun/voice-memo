// main/memo_ui_detail.c —— 详情页。布局见 docs/design/visual-language.zh_CN.md §5。
#include "memo_ui_detail.h"

#include <stdio.h>
#include <string.h>

#include "memo_font.h"
#include "memo_time.h"
#include "memo_ui_common.h"

#define DETAIL_HEADER_H 36
#define DETAIL_BAR_H 48
#define DETAIL_HINT_H 24
#define DETAIL_LINES_COLLAPSED 6

static lv_obj_t *s_screen;
static lv_obj_t *s_header;
static lv_obj_t *s_panel;
static lv_obj_t *s_text;
static lv_obj_t *s_bar;
static lv_obj_t *s_bar_label;
static lv_obj_t *s_bar_track;
static bool s_expanded;

static void apply_geometry(void)
{
    const int width = lv_display_get_horizontal_resolution(NULL);
    const int card_w = width - 2 * VM_INSET - VM_SHADOW;

    const int top = VM_INSET + DETAIL_HEADER_H;
    const int bottom = lv_display_get_vertical_resolution(NULL) - VM_INSET - DETAIL_HINT_H - DETAIL_BAR_H - 6;
    const int full_h = bottom - top;
    const int collapsed_h = 6 * 28 + 2 * 10;

    lv_obj_set_height(s_panel, s_expanded ? full_h : collapsed_h);
    (void)card_w;
}

void memo_ui_detail_build(void)
{
    const int width = lv_display_get_horizontal_resolution(NULL);
    const int card_w = width - 2 * VM_INSET - VM_SHADOW;

    s_screen = lv_obj_create(NULL);
    memo_ui_flat_rect(s_screen, VM_PAPER, 0);
    lv_obj_set_style_radius(s_screen, 0, 0);

    s_header = memo_ui_label(s_screen, memo_font_label(), VM_INK);
    lv_label_set_text(s_header, "‹ --:--");
    lv_obj_set_pos(s_header, VM_INSET + 4, VM_INSET + 8);

    /* 正文面板：圆角 14，内部可滚动。 */
    lv_obj_t *panel_shadow = NULL;
    s_panel = memo_ui_card(s_screen, VM_INSET, VM_INSET + DETAIL_HEADER_H, card_w,
                           6 * 28 + 2 * 10, &panel_shadow);
    lv_obj_set_style_radius(s_panel, VM_RADIUS_PANEL, 0);
    lv_obj_set_style_pad_all(s_panel, 10, 0);
    lv_obj_add_flag(s_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_panel, LV_DIR_VER);

    s_text = memo_ui_label(s_panel, memo_font_body(), VM_INK);
    lv_label_set_long_mode(s_text, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_text, card_w - 24);
    lv_obj_set_style_text_line_space(s_text, 8, 0);
    lv_label_set_text(s_text, "");

    /* 播放条：胶囊形，整页唯一的非卡片形状。 */
    lv_obj_t *bar_shadow = NULL;
    s_bar = memo_ui_card(s_screen, VM_INSET, 0, card_w, DETAIL_BAR_H, &bar_shadow);
    lv_obj_set_style_radius(s_bar, DETAIL_BAR_H / 2, 0);
    lv_obj_align(s_bar, LV_ALIGN_BOTTOM_LEFT, VM_INSET, -VM_INSET - DETAIL_HINT_H + 2);
    lv_obj_align(bar_shadow, LV_ALIGN_BOTTOM_LEFT, VM_INSET + VM_SHADOW,
                 -VM_INSET - DETAIL_HINT_H + 2 + VM_SHADOW);

    s_bar_label = memo_ui_label(s_bar, memo_font_label(), VM_INK);
    lv_label_set_text(s_bar_label, "▶  0:00 / 0:00");
    lv_obj_align(s_bar_label, LV_ALIGN_LEFT_MID, 14, 0);

    s_bar_track = lv_obj_create(s_bar);
    memo_ui_flat_rect(s_bar_track, VM_PAPER, 2);
    lv_obj_set_style_border_width(s_bar_track, 1, 0);
    lv_obj_set_style_border_color(s_bar_track, lv_color_hex(VM_INK), 0);
    lv_obj_set_size(s_bar_track, 74, 6);
    lv_obj_align(s_bar_track, LV_ALIGN_RIGHT_MID, -14, 0);

    lv_obj_t *hint = memo_ui_label(s_screen, memo_font_label(), VM_MUTED);
    lv_label_set_text(hint, "OK 展开/播放   长按返回");
    lv_obj_align(hint, LV_ALIGN_BOTTOM_LEFT, VM_INSET + 4, -VM_INSET - 2);
}

void memo_ui_detail_show(int64_t recorded_at, uint32_t duration_ms, const char *transcript)
{
    const int32_t tz = 480;
    char clock[8] = "--:--";
    char duration[16] = "0:00";
    if (recorded_at > 0) {
        memo_time_clock(recorded_at, tz, clock, sizeof(clock));
        memo_time_duration((int64_t)duration_ms, duration, sizeof(duration));
    }

    char header[40];
    snprintf(header, sizeof(header), "‹ %s · %s", clock, duration);
    lv_label_set_text(s_header, header);

    if (transcript != NULL && transcript[0] != '\0') {
        lv_label_set_text(s_text, transcript);
    } else {
        /* 还没连服务端时也要如实说明，而不是留一片空白。 */
        lv_label_set_text(s_text, "这条录音已保存在设备上。\n"
                                  "连上服务器后会显示转写文字，原始录音现在就能播放。");
    }

    s_expanded = false;
    apply_geometry();
    lv_obj_scroll_to_y(s_panel, 0, LV_ANIM_OFF);
    memo_ui_detail_set_playback(false, 0, duration_ms);
}

void memo_ui_detail_set_expanded(bool expanded)
{
    s_expanded = expanded;
    apply_geometry();
}

void memo_ui_detail_set_playback(bool playing, uint32_t position_ms, uint32_t duration_ms)
{
    char pos[16] = "0:00";
    char dur[16] = "0:00";
    memo_time_duration((int64_t)position_ms, pos, sizeof(pos));
    memo_time_duration((int64_t)duration_ms, dur, sizeof(dur));

    char text[48];
    snprintf(text, sizeof(text), "%s  %s / %s", playing ? "❚❚" : "▶", pos, dur);
    lv_label_set_text(s_bar_label, text);

    /* 进度用一根短轨道表示，避免为 4px 的细进度条再造一种控件。 */
    int width = 74;
    if (duration_ms > 0) {
        uint32_t pct = (uint32_t)((uint64_t)position_ms * 100 / duration_ms);
        if (pct > 100) pct = 100;
        width = 8 + (int)(pct * 66 / 100);
    }
    lv_obj_set_width(s_bar_track, width);
}

lv_obj_t *memo_ui_detail_screen(void)
{
    return s_screen;
}
