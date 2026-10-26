// main/memo_ui_settings.c —— 设置页。七行，见产品方案 §4.5。
#include "memo_ui_settings.h"

#include <stdio.h>

#include "memo_font.h"
#include "memo_settings.h"
#include "memo_spool.h"
#include "memo_ui_common.h"

/* 行定义 */
enum {
    ROW_NETWORK = 0,
    ROW_SERVER,
    ROW_VOLUME,
    ROW_TIMEOUT,
    ROW_SLEEP,
    ROW_AUTOPLAY,
    ROW_ABOUT,
    ROW_COUNT,
};

#define ROW_H 36
#define ROW_PITCH 40

static lv_obj_t *s_screen;
static lv_obj_t *s_rows[ROW_COUNT];
static lv_obj_t *s_row_shadows[ROW_COUNT];
static lv_obj_t *s_values[ROW_COUNT];
static lv_obj_t *s_hint;
static int s_selected;

/* 子页覆盖层 */
static lv_obj_t *s_sheet;
static lv_obj_t *s_sheet_shadow;
static lv_obj_t *s_sheet_title;
static lv_obj_t *s_sheet_value;
static bool s_in_sheet;

static const char *row_label(int row)
{
    switch (row) {
    case ROW_NETWORK: return "网络";
    case ROW_SERVER: return "服务器";
    case ROW_VOLUME: return "音量";
    case ROW_TIMEOUT: return "自动息屏";
    case ROW_SLEEP: return "深度休眠";
    case ROW_AUTOPLAY: return "自动播放";
    default: return "关于";
    }
}

static void format_value(int row, char *buf, size_t cap)
{
    const memo_settings_t *st = memo_settings_get();
    switch (row) {
    case ROW_NETWORK:
        snprintf(buf, cap, "未配置");
        break;
    case ROW_SERVER:
        snprintf(buf, cap, "未发现");
        break;
    case ROW_VOLUME:
        snprintf(buf, cap, "%u%%", st->volume);
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
    default:
        snprintf(buf, cap, "v0.1");
        break;
    }
}

static void refresh_rows(void)
{
    for (int row = 0; row < ROW_COUNT; row++) {
        char value[24];
        format_value(row, value, sizeof(value));
        lv_label_set_text(s_values[row], value);
        lv_obj_set_style_bg_color(s_rows[row],
                                  lv_color_hex(row == s_selected ? VM_SUN : VM_CARD), 0);
        lv_obj_set_style_translate_y(s_rows[row], row == s_selected ? -2 : 0, 0);
    }
}

static void sheet_visible(bool visible)
{
    memo_ui_card_visible(s_sheet, s_sheet_shadow, visible);
    s_in_sheet = visible;
    lv_obj_set_style_text_color(s_hint, lv_color_hex(VM_MUTED), 0);
    lv_label_set_text(s_hint, visible ? "上下调值 · OK 保存 · 长按取消" : "上下选择 · OK 进入 · 长按返回");
}

void memo_ui_settings_build(void)
{
    const int width = lv_display_get_horizontal_resolution(NULL);
    const int card_w = width - 2 * VM_INSET - VM_SHADOW;

    s_screen = lv_obj_create(NULL);
    memo_ui_flat_rect(s_screen, VM_PAPER, 0);
    lv_obj_set_style_radius(s_screen, 0, 0);

    lv_obj_t *title = memo_ui_label(s_screen, memo_font_label(), VM_INK);
    lv_label_set_text(title, "设置");
    lv_obj_set_pos(title, VM_INSET + 4, VM_INSET + 4);

    lv_obj_t *divider = lv_obj_create(s_screen);
    memo_ui_flat_rect(divider, VM_INK, 0);
    lv_obj_set_size(divider, width - 2 * VM_INSET, 2);
    lv_obj_set_pos(divider, VM_INSET, 34);

    for (int row = 0; row < ROW_COUNT; row++) {
        lv_obj_t *obj = memo_ui_card(s_screen, VM_INSET, 42 + row * ROW_PITCH,
                                     card_w, ROW_H, &s_row_shadows[row]);
        s_rows[row] = obj;

        lv_obj_t *label = memo_ui_label(obj, memo_font_label(), VM_INK);
        lv_label_set_text(label, row_label(row));
        lv_obj_align(label, LV_ALIGN_LEFT_MID, 8, 0);

        s_values[row] = memo_ui_label(obj, memo_font_label(), VM_MUTED);
        lv_obj_align(s_values[row], LV_ALIGN_RIGHT_MID, -8, 0);
    }

    s_hint = memo_ui_label(s_screen, memo_font_label(), VM_MUTED);
    lv_obj_align(s_hint, LV_ALIGN_BOTTOM_LEFT, VM_INSET + 4, -VM_INSET - 2);
    lv_label_set_text(s_hint, "上下选择 · OK 进入 · 长按返回");

    /* 子页：一张居中的卡片，显示当前项与值。 */
    s_sheet = memo_ui_card(s_screen, VM_INSET + 16, 96, card_w - 32, 96, &s_sheet_shadow);
    lv_obj_set_style_radius(s_sheet, VM_RADIUS_PANEL, 0);

    s_sheet_title = memo_ui_label(s_sheet, memo_font_label(), VM_MUTED);
    lv_obj_align(s_sheet_title, LV_ALIGN_TOP_MID, 0, 10);

    s_sheet_value = memo_ui_label(s_sheet, memo_font_body(), VM_INK);
    lv_obj_align(s_sheet_value, LV_ALIGN_CENTER, 0, 10);

    memo_ui_card_visible(s_sheet, s_sheet_shadow, false);
}

void memo_ui_settings_refresh(void)
{
    refresh_rows();
    memo_ui_card_visible(s_sheet, s_sheet_shadow, false);
    s_in_sheet = false;
    lv_label_set_text(s_hint, "上下选择 · OK 进入 · 长按返回");
}

int memo_ui_settings_move(int direction)
{
    if (s_in_sheet) return s_selected;
    s_selected += direction;
    if (s_selected < 0) s_selected = 0;
    if (s_selected >= ROW_COUNT) s_selected = ROW_COUNT - 1;
    refresh_rows();
    return s_selected;
}

int memo_ui_settings_selected(void)
{
    return s_selected;
}

bool memo_ui_settings_row_editable(int row)
{
    return row == ROW_VOLUME || row == ROW_TIMEOUT || row == ROW_SLEEP || row == ROW_AUTOPLAY;
}

void memo_ui_settings_enter(void)
{
    const memo_settings_t *st = memo_settings_get();

    if (s_selected == ROW_AUTOPLAY) {
        memo_settings_set_autoplay(!st->autoplay);
        refresh_rows();
        return;
    }
    if (!memo_ui_settings_row_editable(s_selected)) {
        return; /* 网络/服务器/关于：只读 */
    }

    lv_label_set_text(s_sheet_title, row_label(s_selected));
    char value[24];
    format_value(s_selected, value, sizeof(value));
    lv_label_set_text(s_sheet_value, value);
    sheet_visible(true);
}

void memo_ui_settings_adjust(int direction)
{
    if (!s_in_sheet) return;

    const memo_settings_t *st = memo_settings_get();
    switch (s_selected) {
    case ROW_VOLUME:
        memo_settings_set_volume(memo_settings_volume_step(st->volume, direction));
        break;
    case ROW_TIMEOUT:
        memo_settings_set_screen_timeout(memo_settings_timeout_step(st->screen_timeout_s, direction));
        break;
    case ROW_SLEEP:
        memo_settings_set_sleep(memo_settings_sleep_step(st->sleep_min, direction));
        break;
    default:
        return;
    }

    char value[24];
    format_value(s_selected, value, sizeof(value));
    lv_label_set_text(s_sheet_value, value);
    refresh_rows();
}

bool memo_ui_settings_in_sheet(void)
{
    return s_in_sheet;
}

lv_obj_t *memo_ui_settings_screen(void)
{
    return s_screen;
}
