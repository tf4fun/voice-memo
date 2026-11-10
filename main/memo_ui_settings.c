// main/memo_ui_settings.c —— 设置页及编辑浮层。
#include "memo_ui_settings.h"

#include <stdio.h>
#include <string.h>

#include "memo_font.h"
#include "memo_browse.h"
#include "memo_text.h"
#include "memo_discovery.h"
#include "memo_net.h"
#include "memo_upload.h"
#include "memo_settings.h"
#include "memo_spool.h"
#include "memo_ui_common.h"
#include "memo_progress.h"

/* 行定义 */
enum {
    ROW_NETWORK = 0,
    ROW_SERVER,
    ROW_VOLUME,
    ROW_BRIGHTNESS,
    ROW_TIMEOUT,
    ROW_SLEEP,
    ROW_AUTOPLAY,
    ROW_AUTOSAVE,
    ROW_ABOUT,
    ROW_COUNT,
};

static lv_obj_t *s_screen;
static lv_obj_t *s_rows[ROW_COUNT];
static lv_obj_t *s_row_shadows[ROW_COUNT];
static lv_obj_t *s_values[ROW_COUNT];
static lv_obj_t *s_hint;
static lv_obj_t *s_position;
static lv_obj_t *s_scroll;
static bool s_row_style_selected[ROW_COUNT];
static int s_selected;
static int s_top;

/* 子页覆盖层 */
static lv_obj_t *s_sheet;
static lv_obj_t *s_sheet_shadow;
static lv_obj_t *s_sheet_title;
static lv_obj_t *s_sheet_value;
static bool s_in_sheet;
static memo_settings_t s_draft;
static lv_obj_t *s_meter_track;
static lv_obj_t *s_meter_fill;
static int s_meter_target = -1;
static lv_obj_t *s_sheet_note;

static void meter_anim(void *obj, int32_t width)
{
    lv_obj_set_width(obj, width);
}

void memo_ui_settings_destroy(void)
{
    if (s_meter_fill != NULL) lv_anim_delete(s_meter_fill, meter_anim);
    memo_ui_screen_delete(s_screen);
    s_screen = s_hint = s_position = s_scroll = s_sheet = s_sheet_shadow = NULL;
    s_sheet_title = s_sheet_value = s_meter_track = s_meter_fill = s_sheet_note = NULL;
    memset(s_rows, 0, sizeof(s_rows));
    memset(s_row_shadows, 0, sizeof(s_row_shadows));
    memset(s_values, 0, sizeof(s_values));
    memset(s_row_style_selected, 0, sizeof(s_row_style_selected));
    s_in_sheet = false;
    s_meter_target = -1;
    s_top = 0;
}

static void refresh_meter_bar(bool animate)
{
    const bool meter = s_selected == ROW_VOLUME || s_selected == ROW_BRIGHTNESS;
    const bool sleep = s_selected == ROW_SLEEP;
    lv_obj_set_hidden(s_sheet_note, !sleep);
    if (sleep) lv_label_set_text(s_sheet_note, s_draft.screen_timeout_s == 0 ?
                                 "常亮时不休眠" : "熄屏后开始计时");
    lv_obj_set_hidden(s_meter_track, !meter);
    if (!meter) {
        lv_anim_delete(s_meter_fill, meter_anim);
        s_meter_target = -1;
        return;
    }
    const int target = memo_progress_width(160,
        s_selected == ROW_BRIGHTNESS ? s_draft.brightness : s_draft.volume, 100);
    if (target == s_meter_target) return; // Heartbeats must not restart motion.
    s_meter_target = target;
    lv_anim_delete(s_meter_fill, meter_anim);
    if (!animate) {
        lv_obj_set_width(s_meter_fill, target);
        return;
    }
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, s_meter_fill);
    lv_anim_set_values(&animation, lv_obj_get_width(s_meter_fill), target);
    lv_anim_set_duration(&animation, 160);
    lv_anim_set_exec_cb(&animation, meter_anim);
    lv_anim_start(&animation);
}

static const char *row_label(int row)
{
    switch (row) {
    case ROW_NETWORK: return "网络";
    case ROW_SERVER: return "服务器";
    case ROW_VOLUME: return "音量";
    case ROW_BRIGHTNESS: return "亮度";
    case ROW_TIMEOUT: return "自动息屏";
    case ROW_SLEEP: return "深度休眠";
    case ROW_AUTOPLAY: return "自动播放";
    case ROW_AUTOSAVE: return "自动保存";
    default: return "关于";
    }
}

static void format_value(int row, char *buf, size_t cap)
{
    const memo_settings_t *st = s_in_sheet ? &s_draft : memo_settings_get();
    switch (row) {
    case ROW_NETWORK:
        switch (memo_net_state()) {
        case MEMO_NET_CONNECTED: {
            memo_network_info_t info;
            memo_net_info(&info);
            snprintf(buf, cap, "%s", info.ssid);
            break;
        }
        case MEMO_NET_CONNECTING:
            snprintf(buf, cap, "连接中");
            break;
        case MEMO_NET_NO_CREDENTIALS:
            snprintf(buf, cap, "未配置");
            break;
        default:
            snprintf(buf, cap, "未连接");
            break;
        }
        break;
    case ROW_SERVER:
        if (memo_upload_discovery_pending()) {
            snprintf(buf, cap, "%s", memo_net_has_ip() ? "查找中" : "等待网络");
            break;
        }
        /* 显示实际发现到的服务端，而不是一句写死的"未发现"。 */
        switch (memo_discovery_state()) {
        case MEMO_DISCOVERY_FOUND: {
            memo_endpoint_t endpoint;
            if (memo_discovery_endpoint(&endpoint)) snprintf(buf, cap, "%s:%u", endpoint.host, (unsigned)endpoint.port);
            else snprintf(buf, cap, "--");
            break;
        }
        case MEMO_DISCOVERY_SEARCHING:
            snprintf(buf, cap, "查找中");
            break;
        default:
            snprintf(buf, cap, "未发现");
            break;
        }
        break;
    case ROW_VOLUME:
        snprintf(buf, cap, "%u%%", st->volume);
        break;
    case ROW_BRIGHTNESS:
        snprintf(buf, cap, "%u%%", st->brightness);
        break;
    case ROW_TIMEOUT:
        if (st->screen_timeout_s == 0) {
            snprintf(buf, cap, "常亮");
        } else if (st->screen_timeout_s < 60) {
            snprintf(buf, cap, "%u 秒", st->screen_timeout_s);
        } else {
            snprintf(buf, cap, "%u 分钟", st->screen_timeout_s / 60);
        }
        break;
    case ROW_SLEEP:
        snprintf(buf, cap, st->sleep_min == 0 ? "不自动" : "%u 分钟", st->sleep_min);
        break;
    case ROW_AUTOPLAY:
        snprintf(buf, cap, "%s", st->autoplay ? "开" : "关");
        break;
    case ROW_AUTOSAVE:
        snprintf(buf, cap, "%s", st->autosave ? "开" : "关");
        break;
    default:
        snprintf(buf, cap, "v0.1");
        break;
    }
}

static void refresh_rows(void)
{
    const memo_rows_layout_t layout = memo_settings_layout(lv_display_get_horizontal_resolution(NULL), lv_display_get_vertical_resolution(NULL));
    s_top = memo_viewport_top(s_top, s_selected, ROW_COUNT, layout.rows);
    for (int row = 0; row < ROW_COUNT; row++) {
        const bool visible = !s_in_sheet && row >= s_top && row < s_top + layout.rows;
        const bool selected = visible && row == s_selected;
        memo_ui_card_visible(s_rows[row], s_row_shadows[row], visible);
        const lv_label_long_mode_t mode = selected ? LV_LABEL_LONG_SCROLL_CIRCULAR : LV_LABEL_LONG_DOT;
        // Even setting the same mode deletes LVGL's marquee animation and
        // resets its offset. Preserve it across the normal input heartbeat.
        if (lv_label_get_long_mode(s_values[row]) != mode)
            lv_label_set_long_mode(s_values[row], mode);
        if (!visible) continue;
        lv_obj_set_pos(s_rows[row], layout.x, layout.top + (row-s_top)*layout.pitch);
        char value[96];
        format_value(row, value, sizeof(value));
        if (strcmp(lv_label_get_text(s_values[row]), value) != 0) memo_font_label_text(s_values[row], value, false);
        /* 选中行整行反白。 */
        if (s_row_style_selected[row] != selected) {
            s_row_style_selected[row] = selected;
            memo_ui_inverse(s_rows[row], selected);
            lv_obj_set_style_text_color(lv_obj_get_child(s_rows[row], 0),
                                        lv_color_hex(selected ? TERM_BG : TERM_FG), 0);
            lv_obj_set_style_text_color(s_values[row], lv_color_hex(selected ? TERM_BG : TERM_DIM), 0);
        }
    }
    char position[16];
    snprintf(position, sizeof(position), "%d/%d", s_selected + 1, ROW_COUNT);
    if (strcmp(lv_label_get_text(s_position), position) != 0) lv_label_set_text(s_position, position);
    memo_ui_scrollbar_update(s_scroll, layout.rows * layout.pitch - (layout.pitch - layout.row_height),
                             ROW_COUNT, layout.rows, s_top);
    if (s_in_sheet) lv_obj_set_hidden(s_scroll, true);
}

static void sheet_visible(bool visible)
{
    memo_ui_card_visible(s_sheet, s_sheet_shadow, visible);
    s_in_sheet = visible;
    if (!visible) {
        lv_anim_delete(s_meter_fill, meter_anim);
        s_meter_target = -1;
    }
    lv_obj_set_style_text_color(s_hint, lv_color_hex(TERM_DIM), 0);
    lv_label_set_text(s_hint, "");
    lv_obj_set_hidden(s_hint, true);
    refresh_rows();
}

void memo_ui_settings_build(void)
{
    if (s_screen != NULL) return;
    const int width = lv_display_get_horizontal_resolution(NULL);
    const int card_w = width - 32;

    s_screen = lv_obj_create(NULL);
    memo_ui_flat_rect(s_screen, TERM_BG, 0);
    lv_obj_set_style_radius(s_screen, 0, 0);

    lv_obj_t *title = memo_ui_header(s_screen, "> 设置");
    lv_obj_set_width(title, 80);
    s_position = memo_ui_label(s_screen, memo_font_label(), TERM_DIM);
    lv_obj_set_pos(s_position, 112, 8);
    lv_obj_set_size(s_position, 64, memo_font_label()->line_height);
    lv_obj_set_style_text_align(s_position, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_long_mode(s_position, LV_LABEL_LONG_CLIP);
    const memo_rows_layout_t layout = memo_settings_layout(width, lv_display_get_vertical_resolution(NULL));

    for (int row = 0; row < ROW_COUNT; row++) {
        lv_obj_t *obj = memo_ui_card(s_screen, layout.x, layout.top + row * layout.pitch,
                                     card_w, layout.row_height, &s_row_shadows[row]);
        s_rows[row] = obj;
        lv_obj_set_style_border_width(obj, 0, 0);

        lv_obj_t *label = memo_ui_label(obj, memo_font_label(), TERM_FG);
        char name[48];
        snprintf(name, sizeof(name), "%02d %s", row + 1, row_label(row));
        lv_label_set_text(label, name);
        lv_obj_set_size(label, 88, memo_font_label()->line_height);
        lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
        lv_obj_align(label, LV_ALIGN_LEFT_MID, 12, 0);

        s_values[row] = memo_ui_label(obj, memo_font_label(), TERM_DIM);
        // DOT mode only truncates within a bounded height. Content height
        // otherwise lets an unselected address wrap onto a second line.
        lv_obj_set_size(s_values[row], 88, memo_font_label()->line_height);
        lv_obj_align(s_values[row], LV_ALIGN_RIGHT_MID, -12, 0);
    }

    s_scroll = memo_ui_scrollbar_create(s_screen, 228, layout.top,
                                        layout.rows * layout.pitch - (layout.pitch - layout.row_height));

    s_hint = memo_ui_label(s_screen, memo_font_label(), TERM_DIM);
    lv_obj_set_pos(s_hint, 24, lv_display_get_vertical_resolution(NULL)-48);
    lv_obj_set_size(s_hint, width-48, memo_font_label()->line_height);
    lv_obj_set_style_bg_color(s_hint, lv_color_hex(TERM_BG), 0);
    lv_obj_set_style_bg_opa(s_hint, LV_OPA_COVER, 0);
    lv_label_set_text(s_hint, "");
    lv_obj_set_hidden(s_hint, true);

    /* 子页：一张居中的卡片，显示当前项与值。 */
    s_sheet = memo_ui_card(s_screen, 24, 100, width - 48, 116, &s_sheet_shadow);
    lv_obj_set_style_border_color(s_sheet, lv_color_hex(TERM_DARK), 0);
    memo_ui_rule(s_sheet, 12, 50, width - 72);

    s_sheet_title = memo_ui_label(s_sheet, memo_font_label(), TERM_DIM);
    lv_obj_set_size(s_sheet_title, width - 72, memo_font_label()->line_height);
    lv_obj_set_style_text_align(s_sheet_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(s_sheet_title, 12, 10);

    s_sheet_value = memo_ui_label(s_sheet, memo_font_body(), TERM_FG);
    lv_obj_set_size(s_sheet_value, width - 72, memo_font_body()->line_height);
    lv_obj_set_style_text_align(s_sheet_value, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(s_sheet_value, 12, 62);

    s_meter_track = memo_ui_meter_create(s_sheet, 160, 12, &s_meter_fill);
    lv_obj_set_style_border_color(s_meter_track, lv_color_hex(TERM_DIM), 0);
    lv_obj_align(s_meter_track, LV_ALIGN_BOTTOM_MID, 0, -16);
    lv_obj_set_hidden(s_meter_track, true);
    s_sheet_note = memo_ui_label(s_sheet, memo_font_label(), TERM_DIM);
    lv_obj_set_size(s_sheet_note, width - 72, memo_font_label()->line_height);
    lv_obj_set_style_text_align(s_sheet_note, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(s_sheet_note, 12, 108);
    lv_obj_set_hidden(s_sheet_note, true);

    memo_ui_card_visible(s_sheet, s_sheet_shadow, false);
}

void memo_ui_settings_refresh(void)
{
    if (s_screen == NULL) return;
    sheet_visible(false);
    refresh_rows();
    lv_obj_set_style_text_color(s_hint, lv_color_hex(TERM_DIM), 0);
    lv_label_set_text(s_hint, "");
}

void memo_ui_settings_refresh_values(void)
{
    if (s_screen == NULL) return;
    refresh_rows();
}

void memo_ui_settings_enter(const memo_settings_t *draft)
{
    if (draft == NULL) return;
    s_draft = *draft;
    const bool expanded = s_selected == ROW_VOLUME || s_selected == ROW_BRIGHTNESS || s_selected == ROW_SLEEP;
    lv_obj_set_pos(s_sheet, 24, expanded ? 92 : 100);
    lv_obj_set_height(s_sheet, expanded ? 148 : 116);
    sheet_visible(true);
    lv_label_set_text(s_sheet_title, row_label(s_selected));
    char value[24];
    format_value(s_selected, value, sizeof(value));
    lv_label_set_text(s_sheet_value, value);
    refresh_meter_bar(false);
}

void memo_ui_settings_show_draft(const memo_settings_t *draft)
{
    if (!s_in_sheet || draft == NULL) return;
    s_draft = *draft;
    sheet_visible(true);
    char value[24];
    format_value(s_selected, value, sizeof(value));
    lv_label_set_text(s_sheet_value, value);
    refresh_meter_bar(true);
    refresh_rows();
}

void memo_ui_settings_error(void)
{
    lv_label_set_text(s_hint, "处理失败");
    lv_obj_set_hidden(s_hint, false);
    lv_obj_set_style_text_color(s_hint, lv_color_hex(TERM_FG), 0);
}

bool memo_ui_settings_in_sheet(void)
{
    return s_in_sheet;
}

lv_obj_t *memo_ui_settings_screen(void)
{
    return s_screen;
}

void memo_ui_settings_set_selected(int selected)
{
    if (selected < 0) selected = 0;
    if (selected >= ROW_COUNT) selected = ROW_COUNT-1;
    s_selected = selected;
}
