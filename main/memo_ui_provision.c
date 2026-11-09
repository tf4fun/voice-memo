// main/memo_ui_provision.c —— 配网页。产品方案 §4.6。
#include "memo_ui_provision.h"

#include <stdio.h>

#include "memo_font.h"
#include "memo_provision.h"
#include "memo_ui_common.h"

static lv_obj_t *s_screen;
static lv_obj_t *s_state;
static lv_obj_t *s_detail;
static lv_obj_t *s_ssid;
static lv_obj_t *s_hint;

void memo_ui_provision_build(void)
{
    if (s_screen != NULL) return;
    const int width = lv_display_get_horizontal_resolution(NULL);
    const int card_w = width - 48;

    s_screen = lv_obj_create(NULL);
    memo_ui_flat_rect(s_screen, TERM_BG, 0);
    lv_obj_set_style_radius(s_screen, 0, 0);

    memo_ui_header(s_screen, "> 蓝牙配网");

    lv_obj_t *panel = memo_ui_card(s_screen, 24, 60, card_w, 196, NULL);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_pad_all(panel, 0, 0);
    memo_ui_rule(panel, 0, 48, card_w);

    s_state = memo_ui_label(panel, memo_font_body(), TERM_FG);
    lv_obj_set_size(s_state, card_w, memo_font_body()->line_height);
    lv_obj_align(s_state, LV_ALIGN_TOP_LEFT, 0, 0);

    s_ssid = memo_ui_label(panel, memo_font_label(), TERM_DIM);
    lv_obj_set_size(s_ssid, card_w, memo_font_label()->line_height);
    lv_label_set_long_mode(s_ssid, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_align(s_ssid, LV_ALIGN_TOP_LEFT, 0, 64);
    lv_obj_set_hidden(s_ssid, true);

    s_detail = memo_ui_label(panel, memo_font_label(), TERM_DIM);
    lv_obj_set_width(s_detail, card_w);
    lv_label_set_long_mode(s_detail, LV_LABEL_LONG_WRAP);
    lv_obj_align(s_detail, LV_ALIGN_TOP_LEFT, 0, 64);

    s_hint = memo_ui_label(s_screen, memo_font_label(), TERM_DIM);
    lv_label_set_text(s_hint, "微信小程序配网");
    lv_obj_set_pos(s_hint, 24, 270);
    lv_obj_set_width(s_hint, width-48);
}

void memo_ui_provision_destroy(void)
{
    memo_ui_screen_delete(s_screen);
    s_screen = s_state = s_detail = s_ssid = s_hint = NULL;
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

    memo_network_info_t info;
    memo_provision_info(&info);
    const bool has_ssid = state == MEMO_PROV_WIFI_CONNECTING || state == MEMO_PROV_WIFI_CONNECTED;
    if (has_ssid) {
        char ssid[40];
        snprintf(ssid, sizeof(ssid), "SSID: %s", info.ssid[0] ? info.ssid : "--");
        memo_font_label_text(s_ssid, ssid, false);
        lv_obj_set_hidden(s_ssid, false);
    } else lv_obj_set_hidden(s_ssid, true);
    lv_obj_set_pos(s_detail, 0, has_ssid ? 104 : 64);
    char detail[160];
    switch (state) {
    case MEMO_PROV_ADVERTISING:
        snprintf(detail, sizeof(detail),
                 "小程序中选择\n%s\n请开启手机蓝牙",
                 MEMO_PROV_DEVICE_NAME);
        break;
    case MEMO_PROV_BLE_CONNECTED:
        snprintf(detail, sizeof(detail), "请在手机里选择 2.4GHz WiFi 并输入密码。");
        break;
    case MEMO_PROV_WIFI_CONNECTING:
        detail[0] = '\0';
        break;
    case MEMO_PROV_WIFI_CONNECTED:
        snprintf(detail, sizeof(detail), "IP: %s", info.ip);
        break;
    case MEMO_PROV_FAILED:
        snprintf(detail, sizeof(detail), "%s",
                 esp_err_to_name(memo_provision_error()));
        break;
    case MEMO_PROV_STARTING:
        snprintf(detail, sizeof(detail), "正在初始化 WiFi 与蓝牙…");
        break;
    default:
        detail[0] = '\0';
        break;
    }
    memo_font_label_text(s_detail, detail, false);
    /* 失败：状态行反白。 */
    const bool failed = state == MEMO_PROV_FAILED;
    lv_obj_set_style_text_color(s_state, lv_color_hex(failed ? TERM_BG : TERM_FG), 0);
    lv_obj_set_style_bg_color(s_state, lv_color_hex(TERM_FG), 0);
    lv_obj_set_style_bg_opa(s_state, failed ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
}

lv_obj_t *memo_ui_provision_screen(void)
{
    return s_screen;
}
