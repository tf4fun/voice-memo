// main/memo_ui_detail.c —— 详情页。布局见 docs/design/visual-language.zh_CN.md §5。
#include "memo_ui_detail.h"

#include <stdio.h>
#include <string.h>

#include "memo_font.h"
#include "memo_follow.h"
#include "memo_progress.h"
#include "memo_settings.h"
#include "memo_time.h"
#include "memo_ui_common.h"

#define DETAIL_BAR_H 32

static lv_obj_t *s_screen;
static lv_obj_t *s_meta;
static lv_obj_t *s_panel;
static lv_obj_t *s_text;
static lv_obj_t *s_bar;
static lv_obj_t *s_bar_label;
static lv_obj_t *s_bar_track;
static lv_obj_t *s_bar_fill;
static lv_obj_t *s_status_label;
static bool s_local;
static char s_status[16];

void memo_ui_detail_build(void)
{
    if (s_screen != NULL) return;
    const int width = lv_display_get_horizontal_resolution(NULL);
    const int card_w = width - 32;

    s_screen = lv_obj_create(NULL);
    memo_ui_flat_rect(s_screen, TERM_BG, 0);
    lv_obj_set_style_radius(s_screen, 0, 0);

    memo_ui_header(s_screen, "> 详情");
    s_meta = memo_ui_label(s_screen, memo_font_mono(), TERM_DIM);
    lv_obj_set_pos(s_meta, 24, 55);
    lv_obj_set_size(s_meta, width - 48, memo_font_mono()->line_height);

    /* 正文：无框阅读区，内部自动滚动。 */
    s_panel = memo_ui_card(s_screen, 24, 80, width - 48, 144, NULL);
    lv_obj_set_style_border_width(s_panel, 0, 0);
    lv_obj_set_style_pad_all(s_panel, 0, 0);
    lv_obj_set_scrollable(s_panel, true);
    lv_obj_set_scroll_dir(s_panel, LV_DIR_VER);

    s_text = memo_ui_label(s_panel, memo_font_body(), TERM_FG);
    lv_label_set_long_mode(s_text, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_text, width - 48);
    lv_obj_set_style_text_line_space(s_text, 8, 0);
    lv_label_set_text(s_text, "");

    /* 播放条：方角面板；播放时整条反白。 */
    s_bar = memo_ui_card(s_screen, 16, 0, card_w, DETAIL_BAR_H, NULL);
    lv_obj_set_style_border_color(s_bar, lv_color_hex(TERM_DARK), 0);
    lv_obj_set_pos(s_bar, 16, 274);

    s_bar_label = memo_ui_label(s_bar, memo_font_mono(), TERM_FG);
    lv_label_set_text(s_bar_label, "> 0:00/0:00");
    lv_obj_align(s_bar_label, LV_ALIGN_LEFT_MID, 14, 0);

    s_bar_track = memo_ui_meter_create(s_bar, 56, 8, &s_bar_fill);
    lv_obj_align(s_bar_track, LV_ALIGN_RIGHT_MID, -14, 0);

    s_status_label = memo_ui_label(s_screen, memo_font_label(), TERM_DIM);
    lv_obj_set_pos(s_status_label, 24, 236);
    lv_obj_set_size(s_status_label, width-48, memo_font_label()->line_height);
    lv_label_set_long_mode(s_status_label, LV_LABEL_LONG_CLIP);


}

void memo_ui_detail_destroy(void)
{
    memo_ui_screen_delete(s_screen);
    s_screen = s_meta = s_panel = s_text = s_bar = s_bar_label = NULL;
    s_bar_track = s_bar_fill = s_status_label = NULL;
    s_local = false;
    s_status[0] = '\0';
}

void memo_ui_detail_show(int64_t recorded_at, uint32_t duration_ms, const char *transcript)
{
    const int32_t tz = memo_settings_get()->tz_offset_min;
    char clock[8] = "--:--";
    char duration[16] = "0:00";
    if (recorded_at > 0) {
        memo_time_clock(recorded_at, tz, clock, sizeof(clock));
    }
    memo_time_duration((int64_t)duration_ms, duration, sizeof(duration));

    char header[40];
    snprintf(header, sizeof(header), "%s / %s", clock, duration);
    lv_label_set_text(s_meta, header);

    lv_label_set_text_static(s_text, "读取中");
    memo_ui_detail_text(transcript != NULL ? transcript : "读取中", false);

    lv_obj_scroll_to_y(s_panel, 0, LV_ANIM_OFF);
    memo_ui_detail_set_playback(false, 0, duration_ms);
}

void memo_ui_detail_mode(memo_state_t state, bool pending, bool error)
{
    (void)state; // Transport state is shown by the playback control.

    const char *status = pending ? "读取中" : error ? "读取失败" :
        s_local ? (!strcmp(s_status,"bad") ? "录音损坏" : "待传") :
        !strcmp(s_status,"failed") ? "转写失败" :
        !strcmp(s_status,"uploaded") ? "待转写" :
        !strcmp(s_status,"transcribing") ? "转写中" : "正文";
    char info[80];
    snprintf(info, sizeof(info), "%s  音量 %u%%", status, memo_settings_get()->volume);
    lv_label_set_text(s_status_label, info);
    lv_obj_set_style_text_color(s_status_label, lv_color_hex(TERM_DIM), 0);
}

bool memo_ui_detail_text(const char *text, bool bottom)
{
    if (!memo_font_label_text(s_text, text, true)) return false;
    lv_obj_update_layout(s_panel);
    lv_obj_scroll_to_y(s_panel, 0, LV_ANIM_OFF);
    if (bottom) lv_obj_scroll_to_y(s_panel, lv_obj_get_scroll_bottom(s_panel), LV_ANIM_OFF);
    return true;
}

void memo_ui_detail_follow(uint32_t target, uint32_t offset, uint32_t end)
{
    lv_obj_update_layout(s_panel);
    const int extent = lv_obj_get_scroll_y(s_panel) + lv_obj_get_scroll_bottom(s_panel);
    lv_obj_scroll_to_y(s_panel, memo_follow_scroll(target, offset, end, extent), LV_ANIM_OFF);
}

void memo_ui_detail_set_playback(bool playing, uint32_t position_ms, uint32_t duration_ms)
{
    char pos[16] = "0:00";
    char dur[16] = "0:00";
    memo_time_duration((int64_t)position_ms, pos, sizeof(pos));
    memo_time_duration((int64_t)duration_ms, dur, sizeof(dur));

    char text[48];
    snprintf(text, sizeof(text), "%s %s/%s",
             playing ? "||" : ">", pos, dur);
    lv_label_set_text(s_bar_label, text);
    /* 播放中整条反白：条/轨道/填充三者的前景与底色互换。 */
    const uint32_t fg = playing ? TERM_BG : TERM_FG;
    lv_obj_set_style_bg_color(s_bar, lv_color_hex(playing ? TERM_FG : TERM_BG), 0);
    lv_obj_set_style_text_color(s_bar_label, lv_color_hex(fg), 0);
    lv_obj_set_style_border_color(s_bar_track, lv_color_hex(fg), 0);
    lv_obj_set_style_bg_color(s_bar_track, lv_color_hex(playing ? TERM_FG : TERM_BG), 0);
    lv_obj_set_style_bg_color(s_bar_fill, lv_color_hex(fg), 0);

    /* 进度用一根短轨道表示，避免为 4px 的细进度条再造一种控件。 */
    lv_obj_set_width(s_bar_fill, memo_progress_width(56, position_ms, duration_ms));
}

lv_obj_t *memo_ui_detail_screen(void)
{
    return s_screen;
}

void memo_ui_detail_status(bool local, const char *status)
{
    s_local = local;
    snprintf(s_status, sizeof(s_status), "%s", status);
}

void memo_ui_detail_message(const char *message)
{
    lv_label_set_text(s_status_label, message);
    lv_obj_set_style_text_color(s_status_label, lv_color_hex(TERM_FG), 0);
}
