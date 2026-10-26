// main/memo_ui_provision.c —— 配网页。产品方案 §4.6。
#include "memo_ui_provision.h"

#include <stdio.h>

#include "memo_font.h"
#include "memo_provision.h"
#include "memo_ui_common.h"

static lv_obj_t *s_screen;
static lv_obj_t *s_state;
static lv_obj_t *s_detail;
static lv_obj_t *s_hint;

void memo_ui_provision_build(void)
{
    const int width = lv_display_get_horizontal_resolution(NULL);
    const int card_w = width - 2 * VM_INSET - VM_SHADOW;

    s_screen = lv_obj_create(NULL);
    memo_ui_flat_rect(s_screen, VM_PAPER, 0);
    lv_obj_set_style_radius(s_screen, 0, 0);

    lv_obj_t *title = memo_ui_label(s_screen, memo_font_label(), VM_INK);
    lv_label_set_text(title, "蓝牙配网");
    lv_obj_set_pos(title, VM_INSET + 4, VM_INSET + 4);

    lv_obj_t *divider = lv_obj_create(s_screen);
    memo_ui_flat_rect(divider, VM_INK, 0);
    lv_obj_set_size(divider, width - 2 * VM_INSET, 2);
    lv_obj_set_pos(divider, VM_INSET, 34);

    lv_obj_t *panel = memo_ui_card(s_screen, VM_INSET, 48, card_w, 176, NULL);
    lv_obj_set_style_radius(panel, VM_RADIUS_PANEL, 0);
    lv_obj_set_style_pad_all(panel, 12, 0);

    s_state = memo_ui_label(panel, memo_font_label(), VM_INK);
    lv_obj_align(s_state, LV_ALIGN_TOP_LEFT, 0, 0);

    s_detail = memo_ui_label(panel, memo_font_label(), VM_MUTED);
    lv_obj_set_width(s_detail, card_w - 48);
    lv_label_set_long_mode(s_detail, LV_LABEL_LONG_WRAP);
    lv_obj_align(s_detail, LV_ALIGN_TOP_LEFT, 0, 34);

    s_hint = memo_ui_label(s_screen, memo_font_label(), VM_MUTED);
    lv_label_set_text(s_hint, "手机用微信小程序连接此设备 · 长按 OK 退出");
    lv_obj_align(s_hint, LV_ALIGN_BOTTOM_LEFT, VM_INSET + 4, -VM_INSET - 2);
}

static const char *state_text(memo_provision_state_t state)
{
    switch (state) {
    case MEMO_PROV_STARTING: return "正在启动…";
    case MEMO_PROV_ADVERTISING: return "等待手机连接";
    case MEMO_PROV_BLE_CONNECTED: return "手机已连接";
    case MEMO_PROV_WIFI_CONNECTING: return "正在连接 WiFi";
    case MEMO_PROV_WIFI_CONNECTED: return "已连接";
    case MEMO_PROV_FAILED: return "失败";
    default: return "未启动";
    }
}

void memo_ui_provision_refresh(void)
{
    if (s_screen == NULL) return;

    const memo_provision_state_t state = memo_provision_state();
    lv_label_set_text(s_state, state_text(state));

    char detail[160];
    switch (state) {
    case MEMO_PROV_ADVERTISING:
        snprintf(detail, sizeof(detail),
                 "小程序里选择：\n%s\n\n若列表为空，请确认手机蓝牙已开启。",
                 MEMO_PROV_DEVICE_NAME);
        break;
    case MEMO_PROV_BLE_CONNECTED:
        snprintf(detail, sizeof(detail), "请在手机里选择 2.4GHz WiFi 并输入密码。");
        break;
    case MEMO_PROV_WIFI_CONNECTING:
        snprintf(detail, sizeof(detail), "SSID: %s", memo_provision_ssid());
        break;
    case MEMO_PROV_WIFI_CONNECTED:
        snprintf(detail, sizeof(detail), "SSID: %s\nIP: %s",
                 memo_provision_ssid(), memo_provision_ip());
        break;
    case MEMO_PROV_FAILED:
        snprintf(detail, sizeof(detail), "%s\n请重试，或长按退出后重新进入。",
                 esp_err_to_name(memo_provision_error()));
        break;
    case MEMO_PROV_STARTING:
        snprintf(detail, sizeof(detail), "正在初始化 WiFi 与蓝牙…");
        break;
    default:
        detail[0] = '\0';
        break;
    }
    lv_label_set_text(s_detail, detail);
    lv_obj_set_style_text_color(s_state,
                                lv_color_hex(state == MEMO_PROV_FAILED ? VM_CORAL : VM_INK), 0);
}

void memo_ui_provision_enter(void)
{
    memo_ui_provision_refresh();
}

lv_obj_t *memo_ui_provision_screen(void)
{
    return s_screen;
}
