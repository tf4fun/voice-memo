// main/memo_app.c —— 语音日记应用外壳（M2：列表空态 + 录音）。
//
// 分工：按键回调只入队；memo_input 任务把事件翻译成状态机输入、执行动作；
// 页面渲染全部在 LVGL 锁内。录音与编码在 recorder 任务里，不占用输入任务。
#include <stdio.h>
#include <stdatomic.h>
#include <string.h>
#include <time.h>

#include "esp_heap_caps.h"
#include "esp_system.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_random.h"
#include "esp_sleep.h"
#include "esp_timer.h"

#include "bsp_audio.h"
#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_pins.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "lvgl.h"

#include "key_guard.h"
#include "memo_container.h"
#include "memo_capture.h"
#include "memo_recorder.h"
#include "memo_spool.h"
#include "memo_font.h"
#include "memo_ui_common.h"
#include "memo_ui_detail.h"
#include "memo_follow.h"
#include "memo_player.h"
#include "memo_settings.h"
#include "memo_api.h"
#include "memo_discovery.h"
#include "memo_events.h"
#include "memo_net.h"
#include "memo_handoff.h"
#include "memo_power.h"
#include "memo_browse.h"
#include "memo_row_tone.h"
#include "memo_notice.h"
#include "memo_provision.h"
#include "memo_ui_provision.h"
#include "memo_ui_settings.h"
#include "memo_upload.h"
#include "memo_rejection_layout.h"
#include "memo_state.h"
#include "memo_time.h"
#include "memo_text.h"

static const char *TAG = "memo_app";

/* ---------- 视觉变量：配色与形状在 memo_ui_common.h ---------- */
#define VM_LEVEL_BARS MEMO_WAVE_SAMPLES

/* ---------- 输入分发 ---------- */
typedef struct {
    memo_input_t input;
    bool physical, press, release;
    int64_t at_us; // Callback time, not time spent waiting in the input queue.
    uint32_t release_epoch;
} input_event_t;
static QueueHandle_t s_queue;
static memo_sm_t s_sm;
static key_guard_t s_guard;
static memo_power_t s_power;
static atomic_bool s_ui_ready;
static bool s_wake_capture;
static int64_t s_autosave_until;
static memo_settings_edit_t s_settings_edit;
static bool s_backlight_on = true;
static bool s_local_playback_held;
static int s_setting_selected;
static bool s_settings_error;
static uint32_t s_confirm_ms;
static int s_soc = -1;
static const char *s_capture_hint = "";

static esp_timer_handle_t s_arm_timer;
static int64_t s_arming_started;
static memo_capture_hold_t s_capture_hold;
static memo_capture_wave_t s_capture_wave;
static bool s_arming_visible;
static atomic_uint s_ok_releases;
static uint32_t s_input_release_epoch, s_arming_release_epoch;
static bool s_capture_prepared;

/* ---------- 页面对象 ---------- */
typedef enum { PAGE_NONE, PAGE_LIST, PAGE_ARMING, PAGE_RECORD, PAGE_DETAIL,
               PAGE_SETTINGS, PAGE_PROVISION } page_kind_t;
static page_kind_t s_ui_page;
static lv_obj_t *s_transition_screen; // One fixed empty screen, allocated before pages.
static lv_obj_t *s_list_screen;
static lv_obj_t *s_record_screen;
static lv_obj_t *s_arming_screen;
static lv_obj_t *s_list_status;
static memo_notice_t s_list_notice;
static lv_obj_t *s_list_hint;
static lv_obj_t *s_timer_label;
static lv_obj_t *s_record_hint;
static lv_obj_t *s_arm_arc;
static lv_obj_t *s_rec_dot;
static lv_obj_t *s_rec_title;
static lv_obj_t *s_capture_caption;
static lv_obj_t *s_input_caption;
static lv_obj_t *s_input_level;
static lv_obj_t *s_wave_baseline;
static lv_obj_t *s_level_bars[VM_LEVEL_BARS];
static lv_obj_t *s_sheet;
static lv_obj_t *s_sheet_shadow;
static lv_obj_t *s_opt_save;
static lv_obj_t *s_opt_cancel;
static char s_last_id[37];
static uint32_t s_detail_duration_ms;  /* 详情页当前条目的时长，播放器未启动时用它 */
static bool s_network_resume_needed;
static bool s_rec_network_suspended;   /* 本次录音是否为了腾内存让出过网络 */
static bool s_rec_workers_paused;
static lv_obj_t *s_reject_sheet;
static lv_obj_t *s_reject_shadow;
static lv_obj_t *s_reject_message;
static lv_obj_t *s_reject_retry;
static lv_obj_t *s_reject_delete;
static char s_reject_id[37];
static bool s_reject_deciding;

/* ---------- 小工具 ---------- */
static void enqueue_input(memo_input_t input);

static void arc_anim_cb(void *obj, int32_t value)
{
    lv_arc_set_rotation((lv_obj_t *)obj, value);
}

static void arm_ring_start(void)
{
    if (s_arm_arc == NULL) return;
    lv_obj_set_hidden(s_arm_arc, false);
    lv_arc_set_value(s_arm_arc, 72);
    lv_arc_set_rotation(s_arm_arc, 0);

    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, s_arm_arc);
    /* Readiness is not a fixed percentage. Keep a moving gap while network,
     * codec and file preparation run, including beyond the minimum hold time. */
    lv_anim_set_values(&anim, 0, 360);
    lv_anim_set_duration(&anim, 900);
    lv_anim_set_repeat_count(&anim, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_exec_cb(&anim, arc_anim_cb);
    lv_anim_start(&anim);
}

static void arm_ring_hide(void)
{
    if (s_arm_arc == NULL) return;
    lv_anim_delete(s_arm_arc, arc_anim_cb);
    lv_obj_set_hidden(s_arm_arc, true);
}

static void arm_timer_cb(void *arg)
{
    (void)arg;
    enqueue_input(MEMO_IN_ARM_COMPLETE);
}

static bool s_warned;

static void refresh_list(void);
static void load_list_model(void);
static void poll_browse(void);
static void act_confirm_save(void);
static void reconcile_ui(void);
static void poll_capture_prepare(void);
static bool detail_state(memo_state_t state) { return state >= MEMO_STATE_DETAIL && state <= MEMO_STATE_PAUSED; }

static memo_handoff_t s_handoff;
static int handoff_stop_events(void *ctx) { (void)ctx; return memo_events_stop(); }
static int handoff_stop_upload(void *ctx) { (void)ctx; return memo_upload_stop(); }
static void handoff_discovery(void *ctx) { (void)ctx; memo_discovery_stop(); }
static int handoff_suspend(void *ctx) { (void)ctx; return memo_net_suspend(); }
static int handoff_start_provision(void *ctx) { (void)ctx; return memo_provision_start(); }
static int handoff_stop_provision(void *ctx) { (void)ctx; return memo_provision_stop(); }
static int handoff_resume(void *ctx)
{
    (void)ctx;
    const memo_net_state_t state = memo_net_state();
    if ((state == MEMO_NET_OFF || state == MEMO_NET_FAILED) && memo_net_start() != ESP_OK) return ESP_FAIL;
    if (memo_discovery_init() != ESP_OK) return ESP_FAIL;
    const esp_err_t upload = memo_upload_start(), events = memo_events_start();
    memo_api_request_refresh();
    return upload == ESP_OK && events == ESP_OK ? ESP_OK : ESP_FAIL;
}
static void resume_network_services(void)
{
    /* Restore the whole pipeline, including after recorder I/O failure. Keep
     * retrying failed starts; a failed STA start is not a recovered network. */
    s_network_resume_needed = handoff_resume(NULL) != ESP_OK;
}

static const memo_handoff_ops_t s_handoff_ops = {
    .stop_events=handoff_stop_events, .stop_upload=handoff_stop_upload,
    .release_discovery=handoff_discovery, .suspend_network=handoff_suspend,
    .start_provision=handoff_start_provision, .stop_provision=handoff_stop_provision,
    .resume=handoff_resume,
};

static void tick_timer_cb(void *arg)
{
    (void)arg;
    if (s_handoff.phase == MEMO_HANDOFF_EXITING || s_handoff.phase == MEMO_HANDOFF_RESUMING) {
        if (memo_handoff_poll(&s_handoff, &s_handoff_ops) && s_sm.state == MEMO_STATE_PROVISIONING) {
            s_sm.state = MEMO_STATE_SETTINGS;
            reconcile_ui();
        }
    }
    if (s_network_resume_needed && !s_rec_network_suspended && !s_rec_workers_paused && s_sm.state != MEMO_STATE_PROVISIONING) {
        resume_network_services();
    }
    if (s_sm.state == MEMO_STATE_AUTOSAVE) {
        if (s_autosave_until && esp_timer_get_time() >= s_autosave_until) {
            s_sm.state = MEMO_STATE_LIST;
            act_confirm_save();
        }
        return;
    }
    if (s_sm.state == MEMO_STATE_UPLOAD_REJECTED) return;

    if (s_sm.state == MEMO_STATE_PROVISIONING) {
        static int64_t connected_at;
        memo_provision_state_t state = memo_provision_state();
        if (state == MEMO_PROV_WIFI_CONNECTED && connected_at == 0) connected_at = esp_timer_get_time();
        if (state != MEMO_PROV_WIFI_CONNECTED) connected_at = 0;
        if (state == MEMO_PROV_FAILED || (connected_at != 0 && esp_timer_get_time() - connected_at >= 2000000)) {
            connected_at = 0;
            if (memo_handoff_end(&s_handoff, &s_handoff_ops)) {
                s_sm.state = MEMO_STATE_SETTINGS;
                reconcile_ui();
                return;
            }
        }
        if (bsp_lvgl_lock(50)) {
            memo_ui_provision_refresh();
            bsp_lvgl_unlock();
        }
        return;
    }

    if (detail_state(s_sm.state)) {
        if (!memo_player_is_active() && (s_sm.state == MEMO_STATE_PLAYING || s_sm.state == MEMO_STATE_PAUSED))
            s_sm.state = MEMO_STATE_DETAIL;
        return;
    }

    if (!memo_state_is_recording(s_sm.state)) return;
    const uint32_t elapsed = memo_recorder_elapsed_ms();
    if (!memo_recorder_is_active() || elapsed >= MEMO_RECORD_LIMIT_MS) {
        ESP_LOGW(TAG, "five minute cap reached, stopping");
        enqueue_input(MEMO_IN_RECORD_LIMIT);
        return;
    }
    if (!s_warned && elapsed >= MEMO_RECORD_WARN_MS) {
        s_warned = true;
        ESP_LOGW(TAG, "recording at %u ms, 30 s left", (unsigned)elapsed);
    }
    const uint8_t hint = memo_recorder_signal_hint();
    s_capture_hint = hint == MEMO_SIGNAL_CLIPPED ? "声音过大" :
                     hint == MEMO_SIGNAL_QUIET ? "声音偏小" : "";
    if (!bsp_lvgl_lock(50)) return;
    if (s_timer_label != NULL) {
        char text[16];
        if (memo_time_timer(memo_recorder_elapsed_ms(), text, sizeof(text))) {
            lv_label_set_text(s_timer_label, text);
        }
        if (memo_recorder_elapsed_ms() >= MEMO_RECORD_WARN_MS) {
            /* 接近上限：计时器反白，终端里的"警报"。 */
            lv_obj_set_style_bg_color(s_timer_label, lv_color_hex(TERM_FG), 0);
            lv_obj_set_style_bg_opa(s_timer_label, LV_OPA_COVER, 0);
            lv_obj_set_style_text_color(s_timer_label, lv_color_hex(TERM_BG), 0);
        }
    }
    if (s_rec_dot != NULL) {
        lv_obj_set_style_bg_color(s_rec_dot,
                                  lv_color_hex((elapsed / 500) % 2 ? TERM_DARK : TERM_FG), 0);
    }
    const uint8_t level = memo_recorder_level();
    memo_capture_wave_push(&s_capture_wave, level);
    for (int i = 0; i < VM_LEVEL_BARS; i++) {
        if (s_level_bars[i] == NULL) continue;
        const int h = memo_capture_wave_height(s_capture_wave.level[i]);
        lv_obj_set_size(s_level_bars[i], 6, h);
        lv_obj_set_pos(s_level_bars[i], 22 + i * 10, 188 - h / 2);
        lv_obj_set_style_bg_color(s_level_bars[i],
                                  lv_color_hex(i >= VM_LEVEL_BARS - 3 ? TERM_FG : TERM_DIM), 0);
    }
    char input[8];
    snprintf(input, sizeof(input), "%03u%%", (unsigned)(level > 100 ? 100 : level));
    if (s_input_level != NULL) lv_label_set_text(s_input_level, input);
    bsp_lvgl_unlock();
}

/* ---------- 页面构建 ---------- */
#define LIST_ROWS 4

static lv_obj_t *s_rows[LIST_ROWS];
static lv_obj_t *s_row_shadows[LIST_ROWS];
static lv_obj_t *s_row_time[LIST_ROWS];
static lv_obj_t *s_row_text[LIST_ROWS];
static lv_obj_t *s_row_dur[LIST_ROWS];
static lv_obj_t *s_row_accent[LIST_ROWS];
static lv_obj_t *s_list_scroll;
static lv_obj_t *s_empty_label;

/* 一屏列表的统一视图：先是本地待上传，然后是服务端返回的历史。 */
typedef struct {
    bool local;      /* true = 本地待上传文件 */
    memo_spool_state_t spool_state;
    char id[37];
    int64_t recorded_at;
    uint32_t duration_ms;
    char text[80];
    char status[16];
    bool has_snippet;
} list_view_t;

#define VIEW_CAPACITY (MEMO_API_MAX_ITEMS + MEMO_SPOOL_MAX_ENTRIES)
static list_view_t s_view[VIEW_CAPACITY];
static memo_api_item_t s_remote_items[MEMO_API_MAX_ITEMS];
static memo_api_page_t s_page;
static list_view_t s_detail_entry;
static memo_api_text_t s_detail_text;
static uint32_t s_page_generation, s_text_generation;
static struct { bool pending; int page; uint32_t generation; } s_detail_navigation;
static int s_list_top, s_local_count;
static bool s_detail_meta_dirty, s_detail_follow;
static memo_notice_t s_detail_notice;
static char s_rendered_revision[65];
static uint32_t s_rendered_offset;
static int64_t s_next_text_poll;

static int s_view_count;
static int s_selected;

static void build_list_page(void)
{
    s_list_screen = lv_obj_create(NULL);
    memo_ui_flat_rect(s_list_screen, TERM_BG, 0);
    lv_obj_set_style_radius(s_list_screen, 0, 0);

    const int width = lv_display_get_horizontal_resolution(NULL);

    memo_ui_header(s_list_screen, "> 备忘");
    const memo_rows_layout_t layout = memo_rows_layout(width, lv_display_get_vertical_resolution(NULL));

    for (int i = 0; i < LIST_ROWS; i++) {
        lv_obj_t *row = memo_ui_card(s_list_screen, layout.x, layout.top + i * layout.pitch,
                                     layout.width, layout.row_height,
                                     &s_row_shadows[i]);
        s_rows[i] = row;
        lv_obj_set_style_border_width(row, 0, 0);
        const memo_list_columns_t columns = memo_list_columns(layout.width);

        s_row_accent[i] = lv_obj_create(row);
        memo_ui_flat_rect(s_row_accent[i], TERM_DIM, 0);
        lv_obj_set_pos(s_row_accent[i], 4, 8);
        lv_obj_set_size(s_row_accent[i], 2, 34);

        s_row_time[i] = memo_ui_label(row, memo_font_mono(), TERM_DIM);
        lv_obj_set_width(s_row_time[i], columns.time_w);
        lv_label_set_long_mode(s_row_time[i], LV_LABEL_LONG_CLIP);
        lv_obj_set_pos(s_row_time[i], columns.time_x, 31);
        lv_obj_set_height(s_row_time[i], memo_font_mono()->line_height);

        s_row_text[i] = memo_ui_label(row, memo_font_label(), TERM_FG);
        lv_obj_set_size(s_row_text[i], columns.text_w, memo_font_label()->line_height);
        lv_label_set_long_mode(s_row_text[i], LV_LABEL_LONG_DOT);
        lv_obj_set_pos(s_row_text[i], columns.text_x, 0);

        s_row_dur[i] = memo_ui_label(row, memo_font_mono(), TERM_DIM);
        lv_obj_set_width(s_row_dur[i], columns.duration_w);
        lv_label_set_long_mode(s_row_dur[i], LV_LABEL_LONG_CLIP);
        lv_obj_set_pos(s_row_dur[i], columns.duration_x, 31);
        lv_obj_set_height(s_row_dur[i], memo_font_mono()->line_height);
    }

    s_list_hint = memo_ui_label(s_list_screen, memo_font_label(), TERM_DIM);
    lv_label_set_text(s_list_hint, "");
    lv_obj_set_pos(s_list_hint, 24, layout.footer_y);
    lv_obj_set_width(s_list_hint, 120);

    s_list_status = memo_ui_label(s_list_screen, memo_font_label(), TERM_DIM);
    lv_label_set_text(s_list_status, "");
    lv_obj_set_pos(s_list_status, width-88, layout.footer_y);
    lv_obj_set_width(s_list_status, 64);
    lv_label_set_long_mode(s_list_status, LV_LABEL_LONG_DOT);

    s_list_scroll = memo_ui_scrollbar_create(s_list_screen, 228, layout.top, 218);
    s_empty_label = memo_ui_label(s_list_screen, memo_font_body(), TERM_DIM);
    lv_obj_set_pos(s_empty_label, 24, 136);
    lv_obj_set_size(s_empty_label, width - 48, memo_font_body()->line_height);
    lv_obj_set_style_text_align(s_empty_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(s_empty_label, "暂无录音");
    lv_obj_set_hidden(s_empty_label, true);
}

static void build_arming_page(void)
{
    s_arming_screen = lv_obj_create(NULL);
    memo_ui_flat_rect(s_arming_screen, TERM_BG, 0);
    lv_obj_set_style_radius(s_arming_screen, 0, 0);
    memo_ui_header(s_arming_screen, "> 准备");

    s_arm_arc = lv_arc_create(s_arming_screen);
    lv_obj_remove_style_all(s_arm_arc);
    lv_obj_set_size(s_arm_arc, 132, 132);
    lv_obj_set_pos(s_arm_arc, 54, 74);
    lv_arc_set_range(s_arm_arc, 0, 100);
    lv_arc_set_bg_angles(s_arm_arc, 0, 360);
    lv_arc_set_value(s_arm_arc, 0);
    lv_obj_set_style_arc_width(s_arm_arc, 6, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_arm_arc, 6, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_arm_arc, lv_color_hex(TERM_DARK), LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_arm_arc, lv_color_hex(TERM_FG), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_arm_arc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_clickable(s_arm_arc, false);
    lv_obj_set_hidden(s_arm_arc, true);
    lv_obj_t *key = memo_ui_label(s_arming_screen, memo_font_mono(), TERM_FG);
    lv_label_set_text(key, "OK");
    lv_obj_set_pos(key, 112, 131);
    lv_obj_t *hint = memo_ui_label(s_arming_screen, memo_font_label(), TERM_FG);
    lv_label_set_text(hint, "即将录音");
    lv_obj_set_pos(hint, 24, 224);
    lv_obj_set_size(hint, 192, memo_font_label()->line_height);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
}

static void build_record_page(void)
{
    s_record_screen = lv_obj_create(NULL);
    memo_ui_flat_rect(s_record_screen, TERM_BG, 0);
    lv_obj_set_style_radius(s_record_screen, 0, 0);

    s_rec_title = memo_ui_header(s_record_screen, "> 录音");
    lv_obj_t *dot = lv_obj_create(s_record_screen);
    memo_ui_flat_rect(dot, TERM_FG, 0);
    lv_obj_set_size(dot, 14, 14);
    lv_obj_set_pos(dot, 16, 16);
    s_rec_dot = dot;

    lv_obj_set_pos(s_rec_title, 38, 8);
    lv_obj_set_width(s_rec_title, 130);

    s_timer_label = memo_ui_label(s_record_screen, &lv_font_montserrat_40, TERM_FG);
    lv_label_set_text(s_timer_label, "00:00");
    lv_obj_set_pos(s_timer_label, 24, 88);
    lv_obj_set_size(s_timer_label, 192, lv_font_montserrat_40.line_height);
    lv_obj_set_style_text_align(s_timer_label, LV_TEXT_ALIGN_CENTER, 0);

    s_capture_caption = memo_ui_label(s_record_screen, memo_font_label(), TERM_DIM);
    lv_label_set_text(s_capture_caption, "捕捉灵感");
    lv_obj_set_pos(s_capture_caption, 24, 48);

    s_wave_baseline = lv_obj_create(s_record_screen);
    memo_ui_flat_rect(s_wave_baseline, TERM_DARK, 0);
    lv_obj_set_pos(s_wave_baseline, 22, 188);
    lv_obj_set_size(s_wave_baseline, 196, 1);
    for (int i = 0; i < VM_LEVEL_BARS; i++) {
        lv_obj_t *bar = lv_obj_create(s_record_screen);
        memo_ui_flat_rect(bar, i >= VM_LEVEL_BARS - 3 ? TERM_FG : TERM_DIM, 0);
        lv_obj_set_size(bar, 6, 2);
        lv_obj_set_pos(bar, 22 + i * 10, 187);
        s_level_bars[i] = bar;
    }
    s_input_caption = memo_ui_label(s_record_screen, memo_font_label(), TERM_DIM);
    lv_label_set_text(s_input_caption, "输入电平");
    lv_obj_set_pos(s_input_caption, 24, 224);
    s_input_level = memo_ui_label(s_record_screen, memo_font_mono(), TERM_FG);
    lv_label_set_text(s_input_level, "000%");
    lv_obj_set_pos(s_input_level, 176, 230);
    memo_ui_rule(s_record_screen, 24, 258, 192);

    s_record_hint = memo_ui_label(s_record_screen, memo_font_label(), TERM_DIM);
    lv_label_set_text(s_record_hint, "");
    lv_obj_set_pos(s_record_hint, 24, 270);
    lv_obj_set_size(s_record_hint, 192, memo_font_label()->line_height);

    /* 松手后弹出的选择条：保存 / 取消 */
    const int width = lv_display_get_horizontal_resolution(NULL);
    s_sheet = memo_ui_card(s_record_screen, 24, 224,
                           width - 48, 52,
                           &s_sheet_shadow);

    lv_obj_set_style_border_width(s_sheet, 0, 0);
    s_opt_save = memo_ui_label(s_sheet, memo_font_body(), TERM_FG);
    lv_label_set_text(s_opt_save, "保存");
    lv_obj_set_style_pad_hor(s_opt_save, 8, 0);
    lv_obj_set_size(s_opt_save, 80, memo_font_body()->line_height);
    lv_obj_set_style_text_align(s_opt_save, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(s_opt_save, 0, 7);

    s_opt_cancel = memo_ui_label(s_sheet, memo_font_body(), TERM_DIM);
    lv_label_set_text(s_opt_cancel, "取消");
    lv_obj_set_style_pad_hor(s_opt_cancel, 8, 0);
    lv_obj_set_size(s_opt_cancel, 80, memo_font_body()->line_height);
    lv_obj_set_style_text_align(s_opt_cancel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(s_opt_cancel, 112, 7);
}

/* 卡片和它的投影是两个对象，必须一起藏，否则页面上会留下一个黑方块。 */
static void sheet_visible(bool visible)
{
    lv_obj_t *objects[] = {s_sheet, s_sheet_shadow};
    for (size_t i = 0; i < sizeof(objects) / sizeof(objects[0]); i++) {
        if (objects[i] == NULL) continue;
        lv_obj_set_hidden(objects[i], !visible);
    }
}

static void refresh_confirm_sheet(void)
{
    if (s_sheet == NULL) return;
    const bool save = s_sm.confirm_save;
    lv_obj_set_style_text_color(s_opt_save, lv_color_hex(save ? TERM_BG : TERM_DIM), 0);
    lv_obj_set_style_text_color(s_opt_cancel, lv_color_hex(save ? TERM_DIM : TERM_BG), 0);
    lv_obj_set_style_bg_color(s_opt_save, lv_color_hex(TERM_FG), 0);
    lv_obj_set_style_bg_color(s_opt_cancel, lv_color_hex(TERM_FG), 0);
    lv_obj_set_style_bg_opa(s_opt_save, save ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(s_opt_cancel, save ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
}

static void row_highlight(int index, bool selected)
{
    if (index < 0 || index >= LIST_ROWS) return;
    lv_obj_t *row = s_rows[index];
    if (row == NULL) return;

    memo_ui_inverse(row, selected);
}

/* 组装列表视图并重画。本地待上传排在最前，其后是服务端历史。 */
static void load_list_model(void)
{
    char selected_id[37] = "";
    if (s_selected >= 0 && s_selected < s_view_count) strcpy(selected_id, s_view[s_selected].id);
    memo_api_page_copy(s_remote_items, MEMO_API_MAX_ITEMS, &s_page);
    s_view_count = 0;

    memo_spool_entry_t entries[MEMO_SPOOL_MAX_ENTRIES];
    const int local = memo_spool_scan(entries, MEMO_SPOOL_MAX_ENTRIES);
    for (int i = 0; i < local && i < MEMO_SPOOL_MAX_ENTRIES; i++) {
        if (entries[i].state == MEMO_SPOOL_SENT || entries[i].state == MEMO_SPOOL_RECORDING ||
            (entries[i].state == MEMO_SPOOL_DRAFT && s_sm.state != MEMO_STATE_AUTOSAVE)) continue;
        if (s_view_count >= VIEW_CAPACITY) break;
        list_view_t *row = &s_view[s_view_count++];
        memset(row, 0, sizeof(*row));
        row->local = true;
        row->spool_state = entries[i].state;
        strncpy(row->id, entries[i].id, sizeof(row->id) - 1);
        row->recorded_at = (int64_t)entries[i].recorded_at;
        row->duration_ms = entries[i].duration_ms;
        snprintf(row->text, sizeof(row->text), "%s",
                 entries[i].state == MEMO_SPOOL_LEGACY_KEPT ? "待重试" :
                 entries[i].state == MEMO_SPOOL_BAD ? "上传失败" : "待传");
    }

    s_local_count = s_view_count;
    const int remote = s_page.count;
    for (int i = 0; i < remote && s_view_count < VIEW_CAPACITY; i++) {
        const memo_api_item_t *item = &s_remote_items[i];
        /* An accepted upload may keep its local source until playback joins.
         * Keep one row per UUID during that overlap. */
        bool local_copy = false;
        for (int j=0; j<s_local_count; j++) {
            if (!strcmp(s_view[j].id, item->id)) { local_copy = true; break; }
        }
        if (local_copy) continue;
        list_view_t *row = &s_view[s_view_count++];
        memset(row, 0, sizeof(*row));
        row->local = false;
        strcpy(row->status, item->status);
        strncpy(row->id, item->id, sizeof(row->id) - 1);
        row->recorded_at = item->recorded_at;
        row->duration_ms = item->duration_ms;
        row->has_snippet = item->snippet[0] != '\0';
        if (item->snippet[0] != '\0') {
            memo_text_prefix(row->text, sizeof(row->text), item->snippet, SIZE_MAX);
        } else if (strcmp(item->status, "ready") == 0) {
            snprintf(row->text, sizeof(row->text), "无转写");
        } else {
            snprintf(row->text, sizeof(row->text), "%s", strcmp(item->status, "failed") == 0 ? "转写失败" :
                strcmp(item->status, "uploaded") == 0 ? "等待转写" : "转写中");
        }
    }
    if (s_page.generation != s_page_generation) {
        s_page_generation = s_page.generation;
        s_selected = s_page.navigation < 0 ? s_view_count-1 : s_page.navigation == 2 ? 0 : s_local_count;
    } else if (selected_id[0]) {
        for (int i=0; i<s_view_count; i++) if (strcmp(selected_id,s_view[i].id)==0) { s_selected=i; break; }
    }

    if (s_selected >= s_view_count) s_selected = s_view_count > 0 ? s_view_count - 1 : 0;
    if (s_selected < 0) s_selected = 0;

}

static void refresh_list(void)
{
    const int32_t tz = memo_settings_get()->tz_offset_min;
    s_list_top = memo_viewport_top(s_list_top, s_selected, s_view_count, LIST_ROWS);
    for (int i = 0; i < LIST_ROWS; i++) {
        const int index = s_list_top + i;
        const bool used = index < s_view_count && s_sm.state != MEMO_STATE_UPLOAD_REJECTED;
        memo_ui_card_visible(s_rows[i], s_row_shadows[i], used);
        if (!used) continue;

        char clock[8] = "--:--";
        char duration[16] = "0:00";
        if (s_view[index].recorded_at > 0) {
            memo_time_clock(s_view[index].recorded_at, tz, clock, sizeof(clock));
        }
        memo_time_duration((int64_t)s_view[index].duration_ms, duration, sizeof(duration));
        lv_label_set_text(s_row_time[i], clock);
        memo_font_label_text(s_row_text[i], s_view[index].text, false);
        const bool bad_or_legacy = s_view[index].spool_state == MEMO_SPOOL_BAD ||
            s_view[index].spool_state == MEMO_SPOOL_LEGACY_KEPT;
        const memo_row_tone_t tone = memo_row_tone(s_view[index].local, bad_or_legacy,
            s_view[index].status, s_view[index].has_snippet);
        /* 单色终端：选中行整行反白；其余靠亮度与色条宽度区分状态。 */
        const bool selected = index == s_selected;
        const memo_row_accent_t accent = memo_row_accent(s_view[index].local, bad_or_legacy,
            s_view[index].status, s_view[index].has_snippet);
        const uint32_t text_color = selected ? TERM_BG : tone == MEMO_TONE_MUTED ? TERM_DIM : TERM_FG;
        const uint32_t side_color = selected ? TERM_BG : TERM_DIM;
        uint32_t accent_color = accent == MEMO_ACCENT_GRAY ? TERM_DARK :
            accent == MEMO_ACCENT_SUN ? TERM_DIM : TERM_FG;
        if (selected) accent_color = TERM_BG;
        lv_obj_set_style_text_color(s_row_text[i], lv_color_hex(text_color), 0);
        lv_obj_set_style_text_color(s_row_time[i], lv_color_hex(side_color), 0);
        lv_obj_set_style_text_color(s_row_dur[i], lv_color_hex(side_color), 0);
        lv_obj_set_style_bg_color(s_row_accent[i], lv_color_hex(accent_color), 0);
        lv_obj_set_size(s_row_accent[i], accent == MEMO_ACCENT_CORAL ? 4 : 2, 34);
        lv_label_set_text(s_row_dur[i], duration);
        row_highlight(i, index == s_selected);
    }

    char status[32];
    if (s_sm.state == MEMO_STATE_AUTOSAVE) snprintf(status, sizeof(status), "%u",
        (unsigned)(s_autosave_until > esp_timer_get_time() ? (s_autosave_until-esp_timer_get_time()+999999)/1000000 : 0));
    else if (s_view_count == 0) status[0] = '\0';
    else if (s_page.loading) snprintf(status, sizeof(status), "读取中");
    else if (s_page.error != ESP_OK) snprintf(status, sizeof(status), "读取失败");
    else snprintf(status, sizeof(status), "%d/%d", s_selected+1, s_view_count);
    lv_label_set_text(s_list_status, status);
    lv_obj_set_style_text_color(s_list_status, lv_color_hex(s_page.error ? TERM_FG : TERM_DIM), 0);
    const bool undo = s_sm.state == MEMO_STATE_AUTOSAVE;
    lv_obj_set_width(s_list_hint, undo ? 168 : 120);
    lv_obj_set_pos(s_list_status, lv_display_get_horizontal_resolution(NULL) - (undo ? 40 : 88), lv_display_get_vertical_resolution(NULL) - 40);
    lv_obj_set_width(s_list_status, undo ? 16 : 64);
    const char *notice = memo_notice_text(&s_list_notice, esp_timer_get_time());
    const bool blocked = memo_spool_recovery_blocked();
    lv_label_set_text(s_list_hint, blocked ? "存储错误" : s_sm.state == MEMO_STATE_AUTOSAVE ? "已保存 · 可撤销" : notice ? notice : "");
    lv_obj_set_style_text_color(s_list_hint, lv_color_hex(blocked || notice ? TERM_FG : TERM_DIM), 0);
    memo_ui_scrollbar_update(s_list_scroll, 218, s_view_count, LIST_ROWS, s_list_top);
    const bool empty = s_view_count == 0 && s_sm.state != MEMO_STATE_UPLOAD_REJECTED;
    lv_obj_set_hidden(s_empty_label, !empty);
    if (empty) {
        lv_label_set_text(s_empty_label, s_page.loading ? "正在读取" : s_page.error ? "读取失败" : "暂无录音");
        lv_obj_set_style_text_color(s_empty_label, lv_color_hex(s_page.error ? TERM_FG : TERM_DIM), 0);
    }
    if (s_sm.state == MEMO_STATE_UPLOAD_REJECTED) lv_obj_set_hidden(s_list_scroll, true);
    lv_obj_set_hidden(s_list_hint, s_sm.state == MEMO_STATE_UPLOAD_REJECTED);
    lv_obj_set_hidden(s_list_status, s_sm.state == MEMO_STATE_UPLOAD_REJECTED);
}

/* 播放当前选中项：本地文件直接播，服务端条目先下载到 SPIFFS 的临时文件。 */
static void play_selected(void)
{
    const list_view_t *row = &s_detail_entry;
    if (row->id[0] == '\0') return;

    if (memo_player_stop() != ESP_OK) return;
    if (s_local_playback_held) { memo_upload_set_playback_busy(false); s_local_playback_held = false; }

    if (row->local) {
        const char *ext = row->spool_state == MEMO_SPOOL_LEGACY_KEPT ? "keep" :
                          row->spool_state == MEMO_SPOOL_BAD ? "bad" : "memo";
        char path[80];
        snprintf(path, sizeof(path), MEMO_SPOOL_MOUNT_POINT "/%.36s.%.4s", row->id, ext);
        memo_upload_set_playback_busy(true);
        s_local_playback_held = true;
        if (memo_player_play_file(path) == ESP_OK) {
            s_detail_follow = true;
            ESP_LOGI(TAG, "playing local %s", row->id);
        } else {
            memo_upload_set_playback_busy(false);
            s_local_playback_held = false;
            ESP_LOGE(TAG, "play failed");
        }
        return;
    }

    /* 下载在播放任务内部完成：按键任务不能在这里阻塞，否则整个输入队列会停。 */
    if (memo_player_play_remote(row->id) == ESP_OK) {
        s_detail_follow = true;
        ESP_LOGI(TAG, "streaming %s from server", row->id);
    } else {
        ESP_LOGE(TAG, "remote play failed");
    }
}

static void open_selected_detail(void)
{
    if (s_selected < 0 || s_selected >= s_view_count) return;
    s_detail_navigation.pending = false;
    s_detail_entry = s_view[s_selected];
    s_detail_duration_ms = s_detail_entry.duration_ms;
    memo_api_cancel_text();
    memset(&s_detail_text, 0, sizeof(s_detail_text));
    s_text_generation = 0;
    s_rendered_revision[0] = '\0';
    s_detail_follow = false;
    s_detail_meta_dirty = true;
    s_detail_notice = (memo_notice_t){0};
    s_next_text_poll = esp_timer_get_time() + 2000000;
    s_sm.state = MEMO_STATE_DETAIL;
    if (!s_detail_entry.local) {
        memo_api_request_text(s_detail_entry.id, 0, false, "");
        memo_api_text_copy(&s_detail_text);
    }
    if (memo_settings_get()->autoplay) {
        play_selected();
        if (memo_player_is_active()) s_sm.state = MEMO_STATE_PLAYING;
    }
}

static void browse_move(int direction)
{
    if (s_page.loading) return;
    const int page = memo_browse_move(&s_selected, s_view_count, direction,
        s_page.previous_cursor[0] != '\0', s_page.next_cursor[0] != '\0');
    if (page && memo_api_request_page(page) == ESP_OK) s_page.loading = true;
}

/* Fetch only at server-provided character boundaries. A replay restarts at zero.
 * Failed reads retain the old text and retry at most once every two seconds. */
static void follow_detail(int64_t now)
{
    if (!s_detail_follow || memo_player_is_paused() || s_detail_entry.local || s_detail_text.pending) return;
    if (s_detail_text.error != ESP_OK && now < s_next_text_poll) return;
    uint32_t duration = memo_player_duration_ms();
    if (!duration) duration = s_detail_duration_ms;
    if (!duration || !s_detail_text.total) return;
    const uint32_t target = memo_follow_target(memo_player_position_ms(), duration, s_detail_text.total);
    uint32_t offset;
    if (target < s_detail_text.offset) offset = 0;
    else if (target >= s_detail_text.end && s_detail_text.end < s_detail_text.total) offset = s_detail_text.end;
    else if (s_detail_text.error != ESP_OK) offset = s_detail_text.offset;
    else return;
    if (memo_api_request_text(s_detail_entry.id, offset, false, s_detail_text.revision) == ESP_OK) {
        s_detail_text.pending = true;
        s_next_text_poll = now + 2000000;
    }
}

static void detail_volume(int direction)
{
    const int current = memo_settings_get()->volume;
    int next = current + direction * 20;
    if (next < 0) next = 0;
    if (next > 100) next = 100;
    if (next != current && memo_settings_set_volume((uint8_t)next) != ESP_OK)
        memo_notice_show(&s_detail_notice, "音量保存失败", esp_timer_get_time());
    else s_detail_notice = (memo_notice_t){0};
}

static void detail_move(int direction)
{
    if (s_detail_navigation.pending) return;
    int index = -1;
    for (int i = 0; i < s_view_count; i++)
        if (!strcmp(s_view[i].id, s_detail_entry.id)) { index = i; break; }
    if (index < 0) {
        memo_notice_show(&s_detail_notice, "列表已更新", esp_timer_get_time());
        return;
    }
    const memo_detail_step_t step = memo_detail_step(index, s_view_count, s_local_count,
        direction, s_page.previous_cursor[0] != '\0', s_page.next_cursor[0] != '\0');
    if (step.index < 0 && !step.request_page) {
        memo_notice_show(&s_detail_notice, direction < 0 ? "已是第一条" : "已是最后一条", esp_timer_get_time());
        return;
    }
    if (memo_player_stop() != ESP_OK || memo_player_is_busy()) {
        memo_notice_show(&s_detail_notice, "音频忙", esp_timer_get_time());
        return; // Keep ownership and the current memo until the worker joins.
    }
    if (s_local_playback_held) {
        memo_upload_set_playback_busy(false);
        s_local_playback_held = false;
    }
    s_detail_follow = false;
    s_sm.state = MEMO_STATE_DETAIL;
    if (!step.request_page) {
        s_selected = step.index;
        open_selected_detail();
        return;
    }
    if (memo_api_request_page(step.page) != ESP_OK) {
        memo_notice_show(&s_detail_notice, "切换失败", esp_timer_get_time());
        return;
    }
    memo_api_cancel_text();
    s_detail_navigation.pending = true;
    s_detail_navigation.page = step.page;
    s_detail_navigation.generation = s_page.generation;
}

static void poll_detail_navigation(void)
{
    if (!s_detail_navigation.pending) return;
    if (!detail_state(s_sm.state)) { s_detail_navigation.pending = false; return; }
    memo_api_page_t page;
    if (!memo_api_page_copy(s_remote_items, MEMO_API_MAX_ITEMS, &page) || page.loading) return;
    s_detail_navigation.pending = false;
    if (page.error != ESP_OK || page.generation == s_detail_navigation.generation) {
        memo_notice_show(&s_detail_notice, "切换失败", esp_timer_get_time());
        return;
    }
    if (page.count == 0) {
        memo_notice_show(&s_detail_notice, "没有更多录音", esp_timer_get_time());
        return;
    }
    load_list_model();
    const int target = s_detail_navigation.page < 0 ? s_view_count - 1 : s_local_count;
    if (target < s_local_count || target >= s_view_count) {
        memo_notice_show(&s_detail_notice, "没有更多录音", esp_timer_get_time());
        return;
    }
    s_selected = target;
    open_selected_detail();
}

static const char *detail_placeholder(void)
{
    if (s_detail_entry.local) return s_detail_entry.spool_state == MEMO_SPOOL_BAD ? "上传失败" : "待传";
    if (s_detail_text.error == ESP_ERR_NOT_FOUND) return "录音不存在";
    if (!memo_net_has_ip()) return "未连接网络";
    if (s_detail_text.pending) return "读取中";
    if (s_detail_text.error != ESP_OK) return "读取失败";
    const char *status = s_detail_text.status[0] ? s_detail_text.status : s_detail_entry.status;
    if (!strcmp(status, "failed")) return "转写失败，可播放";
    if (!strcmp(status, "uploaded")) return "等待转写";
    if (!strcmp(status, "transcribing")) return "转写中";
    return "无转写文字";
}

static void poll_browse(void)
{
    const int64_t now = esp_timer_get_time();
    const bool updated = memo_api_take_updated();
    static int64_t next_list, next_battery;
    if ((s_sm.state == MEMO_STATE_LIST || s_sm.state == MEMO_STATE_AUTOSAVE) && (updated || now >= next_list)) {
        load_list_model();
        next_list = now + 1000000;
    }
    if (now >= next_battery) {
        s_soc = bsp_battery_soc(); // I2C is deliberately outside the LVGL lock.
        next_battery = now + 5000000;
    }
    poll_detail_navigation();
    if (s_detail_navigation.pending) return;
    if (!detail_state(s_sm.state)) return;
    if (s_detail_entry.local) {
        memo_spool_entry_t entries[MEMO_SPOOL_MAX_ENTRIES];
        int n = memo_spool_scan(entries, MEMO_SPOOL_MAX_ENTRIES);
        bool found = false;
        for (int i=0; i<n && i<MEMO_SPOOL_MAX_ENTRIES; i++) if (!strcmp(entries[i].id, s_detail_entry.id)) {
            s_detail_entry.spool_state = entries[i].state; found = true; break;
        }
        if (found || memo_spool_recovery_blocked() || s_local_playback_held) return;
        // This detail cannot delete its recording. After upload releases the
        // transient file, verify/read the same ID remotely instead of replaying
        // a path that no longer exists.
        s_detail_entry.local = false;
        memo_api_request_text(s_detail_entry.id, 0, false, "");
        s_detail_meta_dirty = true;
    }
    memo_api_text_copy(&s_detail_text);
    if (!s_detail_text.pending && s_detail_text.error == ESP_ERR_INVALID_STATE) {
        memo_api_request_text(s_detail_entry.id, 0, false, "");
        s_detail_text.pending = true;
    } else if (!s_detail_text.pending && s_detail_text.error != ESP_OK &&
               s_detail_text.error != ESP_ERR_NOT_FOUND && now >= s_next_text_poll) {
        memo_api_request_text(s_detail_entry.id, s_detail_text.offset, false,
                              s_detail_text.text[0] ? s_detail_text.revision : "");
        s_detail_text.pending = true;
        s_next_text_poll = now + 2000000;
    } else if (!s_detail_text.pending && s_detail_text.error == ESP_OK && now >= s_next_text_poll &&
        (updated || (s_detail_text.text[0] == '\0' && strcmp(s_detail_text.status,"ready") && strcmp(s_detail_text.status,"failed")))) {
        memo_api_request_text(s_detail_entry.id, s_detail_text.offset, false,
                              s_detail_text.text[0] ? s_detail_text.revision : "");
        s_detail_text.pending = true;
        s_next_text_poll = now + 2000000;
    }
    follow_detail(now);
}

static void refresh_capture_page(memo_state_t rendered_state)
{
    const bool recording = s_sm.state == MEMO_STATE_RECORDING;
    lv_label_set_text(s_rec_title, recording ? "> 录音" : "> 保存");
    lv_label_set_text(s_capture_caption, recording ? "捕捉灵感" : "录音完成");
    lv_obj_set_hidden(s_rec_dot, !recording);
    lv_obj_set_hidden(s_input_caption, !recording);
    lv_obj_set_hidden(s_input_level, !recording);
    lv_obj_set_hidden(s_wave_baseline, !recording);
    for (int i = 0; i < VM_LEVEL_BARS; i++) {
        lv_obj_set_hidden(s_level_bars[i], !recording);
        if (recording && rendered_state != MEMO_STATE_RECORDING) {
            lv_obj_set_size(s_level_bars[i], 6, 2);
            lv_obj_set_pos(s_level_bars[i], 22 + i * 10, 187);
        }
    }
    if (recording && rendered_state != MEMO_STATE_RECORDING) {
        lv_label_set_text(s_timer_label, "00:00");
        lv_label_set_text(s_input_level, "000%");
        lv_obj_set_style_bg_color(s_rec_dot, lv_color_hex(TERM_FG), 0);
        lv_obj_set_style_text_color(s_timer_label, lv_color_hex(TERM_FG), 0);
        lv_obj_set_style_bg_opa(s_timer_label, LV_OPA_TRANSP, 0);
    }
    sheet_visible(!recording);
    lv_obj_set_pos(s_record_hint, 24, recording ? 270 : 154);
    lv_obj_set_size(s_record_hint, 192, memo_font_label()->line_height);
    lv_label_set_text(s_record_hint, s_capture_hint);
    if (s_sm.state == MEMO_STATE_CONFIRM) {
        char timer[16]; memo_time_timer(s_confirm_ms, timer, sizeof(timer));
        lv_label_set_text(s_timer_label, timer);
        lv_label_set_text(s_record_hint, s_capture_hint);
        refresh_confirm_sheet();
    }
}

/* Only the input owner changes page lifetime, under the LVGL lock. Workers
 * publish plain data and never retain these objects. Leave no hidden page or
 * animation behind, and delete before allocating the destination's widgets. */
static void release_page(void)
{
    if (s_transition_screen == NULL) {
        s_transition_screen = lv_screen_active(); // Reuse LVGL's boot screen.
        if (s_transition_screen == NULL) s_transition_screen = lv_obj_create(NULL);
        memo_ui_flat_rect(s_transition_screen, TERM_BG, 0);
    }
    lv_screen_load(s_transition_screen);
    arm_ring_hide();
    s_arming_visible = false;
    memo_ui_screen_delete(s_list_screen);
    memo_ui_screen_delete(s_record_screen);
    memo_ui_screen_delete(s_arming_screen);
    memo_ui_detail_destroy();
    memo_ui_settings_destroy();
    memo_ui_provision_destroy();
    s_list_screen = s_record_screen = s_arming_screen = NULL;
    s_list_status = s_list_hint = s_list_scroll = s_empty_label = NULL;
    memset(s_rows, 0, sizeof(s_rows));
    memset(s_row_shadows, 0, sizeof(s_row_shadows));
    memset(s_row_time, 0, sizeof(s_row_time));
    memset(s_row_text, 0, sizeof(s_row_text));
    memset(s_row_dur, 0, sizeof(s_row_dur));
    memset(s_row_accent, 0, sizeof(s_row_accent));
    s_timer_label = s_record_hint = s_arm_arc = s_rec_dot = s_rec_title = NULL;
    s_capture_caption = s_input_caption = s_input_level = s_wave_baseline = NULL;
    memset(s_level_bars, 0, sizeof(s_level_bars));
    s_sheet = s_sheet_shadow = s_opt_save = s_opt_cancel = NULL;
    s_reject_sheet = s_reject_shadow = s_reject_message = NULL;
    s_reject_retry = s_reject_delete = NULL;
    s_ui_page = PAGE_NONE;
}

static void prepare_page(page_kind_t page)
{
    if (s_ui_page == page) return;
    release_page();
    memo_fonts_init();
    switch (page) {
    case PAGE_LIST: build_list_page(); break;
    case PAGE_ARMING: build_arming_page(); break;
    case PAGE_RECORD: build_record_page(); break;
    case PAGE_DETAIL:
        memo_ui_detail_build();
        s_detail_meta_dirty = true;
        s_rendered_revision[0] = '\0';
        break;
    case PAGE_SETTINGS: memo_ui_settings_build(); break;
    case PAGE_PROVISION: memo_ui_provision_build(); break;
    default: break;
    }
    s_ui_page = page;
}

// Retry drawing from authoritative state after a lock timeout. No I/O or
// firmware side effects are repeated; the input owner alone calls this.
static void reconcile_ui(void)
{
    static memo_state_t rendered_state = MEMO_STATE_COUNT;
    if (!bsp_lvgl_lock(50)) return;
    const bool arming_preview = s_sm.state == MEMO_STATE_ARMING &&
        (s_arming_visible || (memo_capture_preview(&s_capture_hold, esp_timer_get_time()) &&
                             uxQueueMessagesWaiting(s_queue) == 0));
    const page_kind_t page = detail_state(s_sm.state) ? PAGE_DETAIL :
        s_sm.state == MEMO_STATE_SETTINGS || s_sm.state == MEMO_STATE_SUB_PAGE ? PAGE_SETTINGS :
        s_sm.state == MEMO_STATE_PROVISIONING ? PAGE_PROVISION :
        s_sm.state == MEMO_STATE_RECORDING || s_sm.state == MEMO_STATE_CONFIRM ? PAGE_RECORD :
        arming_preview ? PAGE_ARMING : PAGE_LIST;
    const bool page_changed = page != s_ui_page;
    if (page_changed) rendered_state = MEMO_STATE_COUNT;
    prepare_page(page);
    memo_ui_battery_update(s_soc);
    if (s_sm.state != MEMO_STATE_ARMING && s_arming_visible) {
        arm_ring_hide();
        s_arming_visible = false;
    }
    lv_obj_t *screen = NULL;
    if (s_sm.state == MEMO_STATE_LIST || s_sm.state == MEMO_STATE_UPLOAD_REJECTED || s_sm.state == MEMO_STATE_AUTOSAVE) {
        screen = s_list_screen;
        // Start undo time only once a render lock has been obtained.
        if (s_sm.state == MEMO_STATE_AUTOSAVE && !s_autosave_until)
            s_autosave_until = esp_timer_get_time() + 3000000;
        refresh_list();
    } else if (s_sm.state == MEMO_STATE_SETTINGS || s_sm.state == MEMO_STATE_SUB_PAGE) {
        memo_ui_settings_set_selected(s_setting_selected);
        if (s_settings_edit.active) {
            if (!memo_ui_settings_in_sheet()) memo_ui_settings_enter(&s_settings_edit.value);
            else memo_ui_settings_show_draft(&s_settings_edit.value);
        } else if (memo_ui_settings_in_sheet()) memo_ui_settings_refresh();
        else memo_ui_settings_refresh_values();
        if (s_settings_error) memo_ui_settings_error();
        screen = memo_ui_settings_screen();
    } else if (s_sm.state == MEMO_STATE_PROVISIONING) {
        memo_ui_provision_refresh();
        screen = memo_ui_provision_screen();
    } else if (detail_state(s_sm.state)) {
        if (s_detail_meta_dirty) {
            memo_ui_detail_show(s_detail_entry.recorded_at, s_detail_entry.duration_ms,
                                s_detail_text.text[0] ? s_detail_text.text : detail_placeholder());
            s_detail_meta_dirty = false;
        }
        if (!s_detail_entry.local && !s_detail_text.pending && s_detail_text.error == ESP_OK &&
            s_detail_text.generation != s_text_generation) {
            bool changed = strcmp(s_rendered_revision, s_detail_text.revision) || s_rendered_offset != s_detail_text.offset;
            if (!changed || memo_ui_detail_text(s_detail_text.text[0] ? s_detail_text.text : detail_placeholder(), false)) {
                s_text_generation = s_detail_text.generation;
                strcpy(s_rendered_revision, s_detail_text.revision);
                s_rendered_offset = s_detail_text.offset;
            }
        } else if (s_detail_text.text[0] == '\0') memo_ui_detail_text(detail_placeholder(), false);
        memo_ui_detail_status(s_detail_entry.local, s_detail_entry.local ?
            (s_detail_entry.spool_state == MEMO_SPOOL_BAD ? "bad" : s_detail_entry.spool_state == MEMO_SPOOL_LEGACY_KEPT ? "legacy" : "") :
            s_detail_text.status[0] ? s_detail_text.status : s_detail_entry.status);
        memo_ui_detail_mode(s_sm.state, s_detail_text.pending, s_detail_text.error != ESP_OK);
        const bool active = memo_player_is_active();
        memo_ui_detail_set_playback(active && !memo_player_is_paused(),
                                   s_detail_follow ? memo_player_position_ms() : 0,
                                   active ? memo_player_duration_ms() : s_detail_duration_ms);
        if (s_detail_follow && !memo_player_is_paused() && !s_detail_text.pending &&
            s_detail_text.error == ESP_OK && s_text_generation == s_detail_text.generation) {
            uint32_t duration = memo_player_duration_ms();
            if (!duration) duration = s_detail_duration_ms;
            if (duration) memo_ui_detail_follow(memo_follow_target(memo_player_position_ms(), duration,
                s_detail_text.total), s_detail_text.offset, s_detail_text.end);
        }
        const char *notice = memo_notice_text(&s_detail_notice, esp_timer_get_time());
        if (s_detail_navigation.pending) memo_ui_detail_message("切换中");
        else if (notice) memo_ui_detail_message(notice);
        screen = memo_ui_detail_screen();
    } else if (s_sm.state == MEMO_STATE_ARMING) {
        screen = s_list_screen;
        if (arming_preview) {
            screen = s_arming_screen;
            if (!s_arming_visible) {
                memo_capture_commit(&s_capture_hold);
                arm_ring_start();
                s_arming_visible = true;
            }
        }
    } else if (s_sm.state == MEMO_STATE_RECORDING || s_sm.state == MEMO_STATE_CONFIRM) {
        screen = s_record_screen;
        refresh_capture_page(rendered_state);
    }
    if (screen != NULL && lv_screen_active() != screen) lv_screen_load(screen);
    rendered_state = s_sm.state;
    bsp_lvgl_unlock();
    if (page_changed) ESP_LOGI(TAG, "page %u: free heap %u (largest block %u)",
                              (unsigned)page, (unsigned)esp_get_free_heap_size(),
                              (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
}

/* ---------- 动作执行 ---------- */
static void act_start_arming(void)
{
    if (memo_player_stop() != ESP_OK || memo_recorder_is_busy()) {
        s_sm.state = MEMO_STATE_LIST;
        memo_notice_show(&s_list_notice, "音频忙", esp_timer_get_time());
        ESP_LOGW(TAG, "audio worker still owns the codec; capture deferred");
        return;
    }
    if (memo_upload_pending()) {
        /* 局部存储是"处理中"状态：上一条没走完就不开始新的一条。 */
        ESP_LOGW(TAG, "recording blocked: previous memo is still being uploaded");
        memo_notice_show(&s_list_notice, "上一条待传", esp_timer_get_time());
        s_sm.state = MEMO_STATE_LIST;
        return;
    }
    if (memo_spool_free_bytes() == 0) {
        memo_notice_show(&s_list_notice, "空间不足", esp_timer_get_time());
        s_sm.state = MEMO_STATE_LIST;
        return;
    }
    s_arming_started = s_capture_hold.pressed_us;
    s_arming_release_epoch = s_input_release_epoch;
    s_arming_visible = false;
    esp_timer_stop(s_arm_timer);
    int64_t remaining = (int64_t)MEMO_ARMING_MS * 1000 - (esp_timer_get_time() - s_arming_started);
    if (remaining < 1) remaining = 1;
    esp_timer_start_once(s_arm_timer, (uint64_t)remaining);
}

static bool capture_key_held(void)
{
    const int windows[][2] = BSP_BTN_MV_TABLE;
    const int mv = bsp_button_read_mv();
    return memo_capture_same_hold(s_arming_release_epoch, atomic_load(&s_ok_releases),
                                 mv >= windows[BSP_BTN_OK][0] && mv < windows[BSP_BTN_OK][1]);
}

static void cancel_capture_prepare(bool failed)
{
    esp_timer_stop(s_arm_timer);
    if (s_capture_prepared) {
        const esp_err_t err = memo_recorder_cancel_prepared();
        if (err != ESP_OK) {
            failed = true;
            ESP_LOGE(TAG, "prepared capture cleanup: %s", esp_err_to_name(err));
        }
        s_capture_prepared = false;
    }
    memo_upload_set_capture_busy(false);
    const bool resume = s_rec_workers_paused || s_rec_network_suspended;
    s_rec_workers_paused = s_rec_network_suspended = false;
    if (resume) resume_network_services();
    s_sm.state = MEMO_STATE_LIST;
    if (failed) memo_notice_show(&s_list_notice, "录音不可用", esp_timer_get_time());
}

static void act_cancel_arming(void)
{
    cancel_capture_prepare(false);
}

/* Runs only in the input owner, after the preparation page is visible and its
 * independently scheduled LVGL animation has started. No extra task/stack. */
static bool prepare_capture_resources(void)
{
    if (s_capture_prepared) return true;
    if (!capture_key_held()) {
        cancel_capture_prepare(false);
        return false;
    }
    const int64_t started = esp_timer_get_time();
    memo_upload_set_capture_busy(true);
    /* A hidden list/SSE result has no value during capture. Join both workers
     * before allocating audio, so requests cannot fragment the same heap. */
    s_rec_workers_paused = true;
    const esp_err_t events_stop = memo_events_stop();
    const esp_err_t upload_stop = memo_upload_stop();
    if (events_stop != ESP_OK || upload_stop != ESP_OK) {
        cancel_capture_prepare(true);
        return false;
    }
    const int64_t joined = esp_timer_get_time();
    if (!capture_key_held()) {
        cancel_capture_prepare(false);
        return false;
    }
    char id[37];
    uint8_t random[16];
    esp_fill_random(random, sizeof(random));
    memo_container_make_uuid(id, random);

    ESP_LOGI(TAG, "recording start: free heap %u (largest block %u)",
             (unsigned)esp_get_free_heap_size(),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    /* 录音要 28 KB 连续栈 + 24.6 KB 编码器。网络常驻后通常仍有余量，
       先直接开；实在不够才让网络暂时让位（并记下来，停止后还要还回去）。 */
    esp_err_t err = memo_recorder_prepare(id);
    if (err == ESP_ERR_NO_MEM) {
        ESP_LOGW(TAG, "no room for the recorder, standing the network down");
        const esp_err_t suspend = memo_net_suspend();
        if (suspend == ESP_OK) {
            s_rec_network_suspended = true;
            err = memo_recorder_prepare(id);
        } else {
            err = suspend;
        }
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "recorder preparation failed: %s", esp_err_to_name(err));
        cancel_capture_prepare(true);
        return false;
    }
    s_capture_prepared = true;
    strncpy(s_last_id, id, sizeof(s_last_id) - 1);
    ESP_LOGI(TAG, "capture preparation: background %u ms, audio/file %u ms",
             (unsigned)((joined-started)/1000), (unsigned)((esp_timer_get_time()-joined)/1000));
    if (!capture_key_held()) {
        cancel_capture_prepare(false);
        return false;
    }
    return true;
}

static void poll_capture_prepare(void)
{
    if (s_sm.state != MEMO_STATE_ARMING || !s_arming_visible ||
        uxQueueMessagesWaiting(s_queue) != 0) return;
    if (!prepare_capture_resources()) {
        reconcile_ui();
        return;
    }
    /* A full input queue may have dropped the one-shot timer event. */
    if (esp_timer_get_time()-s_arming_started >= (int64_t)MEMO_ARMING_MS*1000 &&
        uxQueueMessagesWaiting(s_queue) == 0) enqueue_input(MEMO_IN_ARM_COMPLETE);
}

static void act_begin_recording(void)
{
    /* If dispatch was delayed, keep the preparation screen through the same
     * work instead of entering RECORDING before resources are ready. */
    s_sm.state = MEMO_STATE_ARMING;
    if (!s_capture_prepared) {
        reconcile_ui();
        if (!prepare_capture_resources()) return;
    }
    if (!capture_key_held()) {
        cancel_capture_prepare(false);
        return;
    }
    const esp_err_t err = memo_recorder_begin();
    if (err != ESP_OK) {
        cancel_capture_prepare(true);
        return;
    }
    s_capture_prepared = false;
    s_sm.state = MEMO_STATE_RECORDING;
    memset(&s_capture_wave, 0, sizeof(s_capture_wave));
    s_capture_hint = "";
    s_warned = false;
    ESP_LOGI(TAG, "recording started: %s", s_last_id);
}

static void finish_capture_resources(void)
{
    /* Recreate persistent network workers before the destination's widgets. */
    if (bsp_lvgl_lock(100)) {
        release_page();
        bsp_lvgl_unlock();
    }
    s_rec_network_suspended = false;
    s_rec_workers_paused = false;
    resume_network_services();
}

static void act_stop_and_save(void)
{
    /* 松手（或到点）先落盘，再问用户保存还是取消；此时还没有回列表。 */
    memo_recorder_result_t result;
    esp_err_t err = memo_recorder_stop(true, &result);
    if (err == ESP_OK) {
        s_confirm_ms = result.duration_ms;
        s_capture_hint = "是否保存录音";
        ESP_LOGI(TAG, "recorded %s (%u ms, %u bytes)",
                 result.id, (unsigned)result.duration_ms, (unsigned)result.size_bytes);
    } else {
        ESP_LOGE(TAG, "recorder stop: %s; recoverable data retained", esp_err_to_name(err));
        if (err == ESP_ERR_TIMEOUT) {
            s_sm.state = MEMO_STATE_RECORDING; /* Retry joining, never close a live writer. */
            return;
        }
        s_sm.state = MEMO_STATE_LIST;
        memo_upload_set_capture_busy(false);
    }

    /* Keep requests stopped throughout the save/cancel choice and undo window. */
    if (err != ESP_OK) finish_capture_resources();
    if (err == ESP_OK && memo_settings_get()->autosave) {
        s_sm.state = MEMO_STATE_AUTOSAVE;
        s_autosave_until = 0;
        load_list_model();
        return;
    }
    if (err != ESP_OK)
        memo_notice_show(&s_list_notice, memo_spool_free_bytes() == 0 ? "空间不足" : "录音中断", esp_timer_get_time());
}

static void act_confirm_save(void)
{
    if (memo_spool_move(s_last_id, MEMO_SPOOL_DRAFT, MEMO_SPOOL_PENDING) != ESP_OK) {
        s_capture_hint = "保存失败 · 请重试";
        s_sm.state = MEMO_STATE_CONFIRM;
        ESP_LOGE(TAG, "save failed; draft retained for another choice");
        return;
    }
    s_autosave_until = 0;
    memo_upload_set_capture_busy(false);
    finish_capture_resources();
    memo_notice_show(&s_list_notice, "已保存", esp_timer_get_time());
    load_list_model();
    ESP_LOGI(TAG, "recording saved: %s", s_last_id);
}

static void act_confirm_cancel(void)
{
    s_autosave_until = 0;
    ESP_LOGI(TAG, "recording discarded: %s", s_last_id);
    if (s_last_id[0] != '\0') {
        if (memo_spool_remove(s_last_id, MEMO_SPOOL_DRAFT) != ESP_OK) {
            s_capture_hint = "取消失败 · 请重试";
            s_sm.confirm_save = false;
            s_sm.state = MEMO_STATE_CONFIRM;
            ESP_LOGE(TAG, "cancel failed; draft retained for another choice");
            return;
        }
        s_last_id[0] = '\0';
    }
    memo_upload_set_capture_busy(false);
    finish_capture_resources();
    load_list_model();
}

static void act_back_to_list(void)
{
    s_detail_navigation.pending = false;
    memo_api_cancel_text();
    memo_player_stop();
}

static void act_open_detail(void)
{
    if (s_selected >= s_view_count || s_page.loading) { s_sm.state = MEMO_STATE_LIST; return; }
    open_selected_detail();
}

static void act_open_settings(void)
{
    s_settings_error = false;
}

static void act_settings_move(memo_action_t action)
{
    s_setting_selected += action == MEMO_ACT_SETTINGS_NEXT ? 1 : -1;
    if (s_setting_selected < 0) s_setting_selected = 0;
    if (s_setting_selected > MEMO_SETTING_ABOUT) s_setting_selected = MEMO_SETTING_ABOUT;
    s_settings_error = false;
}

static void enter_network_row(void)
{
    /* 网络行不是子页，而是进入蓝牙配网。 */
    bool started = memo_handoff_begin(&s_handoff, &s_handoff_ops);
    if (started || s_handoff.phase == MEMO_HANDOFF_EXITING) {
        s_sm.state = MEMO_STATE_PROVISIONING;
    } else {
        s_sm.state = MEMO_STATE_SETTINGS;
    }
}

static void enter_toggle_row(memo_setting_row_t row)
{
    const esp_err_t err = row == MEMO_SETTING_AUTOSAVE ?
        memo_settings_set_autosave(!memo_settings_get()->autosave) :
        memo_settings_set_autoplay(!memo_settings_get()->autoplay);
    s_sm.state = MEMO_STATE_SETTINGS;
    s_settings_error = err != ESP_OK;
}

static void enter_edit_row(memo_setting_row_t row)
{
    const memo_settings_field_t field = row == MEMO_SETTING_VOLUME ? MEMO_EDIT_VOLUME :
        row == MEMO_SETTING_TIMEOUT ? MEMO_EDIT_TIMEOUT : MEMO_EDIT_SLEEP;
    s_settings_error = false;
    if (memo_settings_edit_begin(&s_settings_edit, field) != ESP_OK) {
        s_settings_error = true;
        s_sm.state = MEMO_STATE_SETTINGS;
    }
}

static void act_enter_sub_page(void)
{
    const memo_setting_row_t row = (memo_setting_row_t)s_setting_selected;
    if (row == MEMO_SETTING_NETWORK) {
        enter_network_row();
        return;
    }
    if (row == MEMO_SETTING_SERVER) {
        memo_upload_request_discovery();
        s_settings_error = false;
        s_sm.state = MEMO_STATE_SETTINGS;
        return;
    }
    if (row == MEMO_SETTING_AUTOPLAY || row == MEMO_SETTING_AUTOSAVE) {
        enter_toggle_row(row);
        return;
    }
    if (row < MEMO_SETTING_VOLUME || row > MEMO_SETTING_SLEEP) {
        s_sm.state = MEMO_STATE_SETTINGS;
        return;
    }
    enter_edit_row(row);
}

static void act_sub_page_adjust(memo_action_t action)
{
    s_settings_error = false;
    int direction = action == MEMO_ACT_SUB_PAGE_NEXT ? 1 : -1;
    if (s_settings_edit.field == MEMO_EDIT_VOLUME) direction = -direction;
    memo_settings_edit_adjust(&s_settings_edit, direction);
}

static void act_sub_page_save(void)
{
    const esp_err_t err = memo_settings_edit_save(&s_settings_edit);
    s_settings_error = err != ESP_OK;
    if (err != ESP_OK) s_sm.state = MEMO_STATE_SUB_PAGE;
}

static void act_sub_page_cancel(void)
{
    s_settings_error = false;
    memo_settings_edit_cancel(&s_settings_edit);
}

static void act_cancel_provisioning(void)
{
    if (!memo_handoff_end(&s_handoff, &s_handoff_ops)) {
        s_sm.state = MEMO_STATE_PROVISIONING;
        return;
    }
}

static void act_reject_choose(memo_action_t action)
{
    if (!s_reject_deciding &&
        memo_upload_choose(s_reject_id, action == MEMO_ACT_REJECT_DELETE) == ESP_OK) {
        s_reject_deciding = true;
    }
}

static void execute(memo_action_t action)
{
    switch (action) {
    case MEMO_ACT_START_ARMING:
        act_start_arming();
        break;

    case MEMO_ACT_CANCEL_ARMING:
        act_cancel_arming();
        break;

    case MEMO_ACT_BEGIN_RECORDING:
        act_begin_recording();
        break;

    case MEMO_ACT_STOP_AND_SAVE:
        act_stop_and_save();
        break;

    case MEMO_ACT_CONFIRM_TOGGLE:
        /* The state machine changed the choice; reconciliation renders it. */
        break;

    case MEMO_ACT_CONFIRM_SAVE:
        act_confirm_save();
        break;

    case MEMO_ACT_CONFIRM_CANCEL:
        act_confirm_cancel();
        break;

    case MEMO_ACT_SELECT_PREV:
    case MEMO_ACT_SELECT_NEXT:
        browse_move(action == MEMO_ACT_SELECT_NEXT ? 1 : -1);
        break;

    case MEMO_ACT_BACK_TO_LIST:
        act_back_to_list();
        break;

    case MEMO_ACT_OPEN_DETAIL:
        act_open_detail();
        break;

    case MEMO_ACT_PLAY:
        play_selected();
        if (!memo_player_is_active()) s_sm.state = MEMO_STATE_DETAIL;
        break;
    case MEMO_ACT_RESUME:
        if (memo_player_resume() != ESP_OK) s_sm.state = MEMO_STATE_DETAIL;
        break;
    case MEMO_ACT_PAUSE:
        if (memo_player_pause() != ESP_OK) s_sm.state = MEMO_STATE_DETAIL;
        break;

    case MEMO_ACT_VOLUME_UP:
    case MEMO_ACT_VOLUME_DOWN:
        detail_volume(action == MEMO_ACT_VOLUME_UP ? 1 : -1);
        break;

    case MEMO_ACT_DETAIL_PREV:
    case MEMO_ACT_DETAIL_NEXT:
        detail_move(action == MEMO_ACT_DETAIL_NEXT ? 1 : -1);
        break;

    case MEMO_ACT_OPEN_SETTINGS:
        act_open_settings();
        break;

    case MEMO_ACT_SETTINGS_PREV:
    case MEMO_ACT_SETTINGS_NEXT:
        act_settings_move(action);
        break;

    case MEMO_ACT_SUB_PAGE_ENTER:
        act_enter_sub_page();
        break;

    case MEMO_ACT_SUB_PAGE_PREV:
    case MEMO_ACT_SUB_PAGE_NEXT:
        act_sub_page_adjust(action);
        break;

    case MEMO_ACT_SUB_PAGE_SAVE:
        act_sub_page_save();
        break;

    case MEMO_ACT_SUB_PAGE_CANCEL:
        act_sub_page_cancel();
        break;

    case MEMO_ACT_CANCEL_PROVISIONING:
        act_cancel_provisioning();
        break;

    case MEMO_ACT_REJECT_TOGGLE:
        /* The input loop redraws from the state-machine choice. */
        break;

    case MEMO_ACT_REJECT_RETRY:
    case MEMO_ACT_REJECT_DELETE:
        act_reject_choose(action);
        break;

    default:
        break;
    }
}

/* ---------- 输入任务 ---------- */
/* Called only by memo_input. File mutations are queued to the upload worker. */
static void poll_rejected_choice(void)
{
    if (s_list_screen == NULL) return;
    memo_upload_rejection_t review;
    const bool available = memo_upload_rejection(&review);
    if (s_sm.state == MEMO_STATE_UPLOAD_REJECTED && !available) {
        if (!bsp_lvgl_lock(100)) return;
        if (s_reject_sheet != NULL) lv_obj_delete(s_reject_sheet);
        s_reject_sheet = s_reject_shadow = s_reject_message = NULL;
        s_reject_retry = s_reject_delete = NULL;
        s_sm.state = MEMO_STATE_LIST;
        refresh_list();
        bsp_lvgl_unlock();
        s_reject_deciding = false;
        /* A retry can finish after the choice closes; keep list metadata live. */
        return;
    }
    if (!available || (s_sm.state != MEMO_STATE_LIST &&
                       s_sm.state != MEMO_STATE_UPLOAD_REJECTED)) return;
    if (!bsp_lvgl_lock(100)) return;
    if (s_reject_sheet == NULL) {
        const int width = lv_display_get_horizontal_resolution(NULL);
        memo_rejection_layout_t layout;
        if (!memo_rejection_layout(width, lv_display_get_vertical_resolution(NULL),
                                   memo_font_label()->line_height, memo_font_body()->line_height,
                                   &layout)) {
            bsp_lvgl_unlock();
            ESP_LOGE(TAG, "rejection dialog font metrics do not fit the display");
            return;
        }
        s_reject_sheet = memo_ui_card(s_list_screen, layout.x, layout.y, layout.width, layout.height,
                                      &s_reject_shadow);
        lv_obj_set_style_border_color(s_reject_sheet, lv_color_hex(TERM_FG), 0);
        lv_obj_t *title = memo_ui_label(s_reject_sheet, memo_font_body(), TERM_FG);
        lv_label_set_text(title, "上传失败");
        lv_obj_set_pos(title, 12, layout.title_y);
        s_reject_message = memo_ui_label(s_reject_sheet, memo_font_label(), TERM_FG);
        lv_obj_set_width(s_reject_message, layout.width - 24);
        lv_label_set_long_mode(s_reject_message, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_line_space(s_reject_message, 0, 0);
        lv_obj_set_pos(s_reject_message, 12, layout.message_y);
        s_reject_retry = memo_ui_label(s_reject_sheet, memo_font_body(), TERM_FG);
        lv_label_set_text(s_reject_retry, "重试");
        lv_obj_set_pos(s_reject_retry, 12, layout.options_y);
        lv_obj_set_size(s_reject_retry, 80, memo_font_body()->line_height);
        lv_obj_set_style_text_align(s_reject_retry, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_pad_hor(s_reject_retry, 8, 0);
        s_reject_delete = memo_ui_label(s_reject_sheet, memo_font_body(), TERM_DIM);
        lv_label_set_text(s_reject_delete, "删除");
        lv_obj_set_pos(s_reject_delete, layout.width - 92, layout.options_y);
        lv_obj_set_size(s_reject_delete, 80, memo_font_body()->line_height);
        lv_obj_set_style_text_align(s_reject_delete, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_pad_hor(s_reject_delete, 8, 0);
        memo_font_check_text("upload dialog", "上传失败上传被拒绝请选择重试或删除正在处理处理失败请重新选择待重试");
    }
    if (s_sm.state == MEMO_STATE_LIST || strcmp(s_reject_id, review.id) != 0) {
        memo_sm_open_rejection(&s_sm);
        snprintf(s_reject_id, sizeof(s_reject_id), "%s", review.id);
        lv_screen_load(s_list_screen);
    }
    s_reject_deciding = review.deciding;
    char clock[8] = "--:--";
    char duration[16];
    if (review.recorded_at > 0) {
        memo_time_clock((int64_t)review.recorded_at, memo_settings_get()->tz_offset_min,
                        clock, sizeof(clock));
    }
    memo_time_duration(review.duration_ms, duration, sizeof(duration));
    char message[128];
    snprintf(message, sizeof(message), "%s · %s\n%s", clock, duration,
             review.deciding ? "正在处理" :
             review.operation_failed ? "处理失败\n请重新选择" :
             "上传被拒绝\n请选择重试或删除");
    lv_label_set_text(s_reject_message, message);
    const bool del = s_sm.reject_delete;
    lv_obj_set_style_text_color(s_reject_retry, lv_color_hex(del ? TERM_DIM : TERM_BG), 0);
    lv_obj_set_style_text_color(s_reject_delete, lv_color_hex(del ? TERM_BG : TERM_DIM), 0);
    lv_obj_set_style_bg_color(s_reject_retry, lv_color_hex(TERM_FG), 0);
    lv_obj_set_style_bg_color(s_reject_delete, lv_color_hex(TERM_FG), 0);
    lv_obj_set_style_bg_opa(s_reject_retry, del ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(s_reject_delete, del ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    refresh_list();
    memo_ui_card_visible(s_reject_sheet, s_reject_shadow, true);
    bsp_lvgl_unlock();
}

static void enqueue_input(memo_input_t input)
{
    if (s_queue == NULL) return;
    const input_event_t event = {.input = input, .at_us = esp_timer_get_time(),
                                 .release_epoch = atomic_load(&s_ok_releases)};
    if (xQueueSend(s_queue, &event, 0) != pdTRUE) {
        ESP_LOGW(TAG, "input queue full, dropping %s", memo_input_name(input));
    }
}

static void on_key(bsp_btn_t btn, bsp_btn_ev_t ev, void *user)
{
    (void)user;
    static bool wait_fresh_press;
    if (btn == BSP_BTN_OK && ev == BSP_BTN_RELEASE) atomic_fetch_add(&s_ok_releases, 1);

    const bool was_armed = s_guard.armed;
    if (was_armed && s_ui_ready && btn == BSP_BTN_OK &&
        (ev == BSP_BTN_LONG || ev == BSP_BTN_RELEASE)) {
        const bool release = ev == BSP_BTN_RELEASE;
        key_guard_consume(&s_guard, release, esp_timer_get_time());
        wait_fresh_press = true;
        const input_event_t wake = {.input=release ? MEMO_IN_OK_RELEASE : MEMO_IN_OK_PRESS,
                                    .physical=true, .press=!release, .release=release, .at_us=esp_timer_get_time(),
                                    .release_epoch=atomic_load(&s_ok_releases)};
        if (s_queue != NULL) xQueueSend(s_queue, &wake, 0);
        return;
    }
    if (key_guard_consume(&s_guard, ev == BSP_BTN_RELEASE, esp_timer_get_time())) {
        wait_fresh_press = true;
        if (was_armed && !s_guard.armed) {
            ESP_LOGI(TAG, "wake key released, input restored");
        }
        return;
    }
    if (wait_fresh_press) {
        if (ev != BSP_BTN_PRESS) return;
        wait_fresh_press = false;
    }

    /* 只做翻译与入队：这里是 esp_timer 任务，不能阻塞、不能碰 LVGL。 */
    memo_input_t input = MEMO_IN_COUNT;
    if (ev == BSP_BTN_LONG) {
        input = (btn == BSP_BTN_OK) ? MEMO_IN_OK_HOLD
              : (btn == BSP_BTN_UP) ? MEMO_IN_UP_HOLD
              : (btn == BSP_BTN_DOWN) ? MEMO_IN_DOWN_HOLD : MEMO_IN_COUNT;
    } else if (ev == BSP_BTN_CLICK) {
        input = (btn == BSP_BTN_OK) ? MEMO_IN_OK_CLICK
              : (btn == BSP_BTN_UP) ? MEMO_IN_UP_CLICK
              : (btn == BSP_BTN_DOWN) ? MEMO_IN_DOWN_CLICK : MEMO_IN_COUNT;
    } else if (ev == BSP_BTN_PRESS && btn == BSP_BTN_OK) {
        /* 圆环从按下瞬间起算，不再叠加长按判定时间。 */
        input = MEMO_IN_OK_PRESS;
    } else if (ev == BSP_BTN_RELEASE && btn == BSP_BTN_OK) {
        input = MEMO_IN_OK_RELEASE;
    }
    const input_event_t event = {
        .input = input, .physical = true,
        .press = ev == BSP_BTN_PRESS, .release = ev == BSP_BTN_RELEASE,
        .at_us = esp_timer_get_time(),
        .release_epoch = atomic_load(&s_ok_releases),
    };
    if (s_queue != NULL) xQueueSend(s_queue, &event, 0);
}

static bool spool_sleep_safe(void)
{
    if (memo_spool_recovery_blocked()) return false;
    memo_spool_entry_t entries[MEMO_SPOOL_MAX_ENTRIES];
    const int n = memo_spool_scan(entries, MEMO_SPOOL_MAX_ENTRIES);
    if (n < 0 || memo_spool_recovery_blocked()) return false;
    for (int i=0; i<n && i<MEMO_SPOOL_MAX_ENTRIES; i++)
        if (entries[i].state == MEMO_SPOOL_RECORDING || entries[i].state == MEMO_SPOOL_DRAFT) return false;
    return true;
}

static bool s_sleep_files_checked, s_sleep_files_safe;

static bool power_safe(void *ctx)
{
    (void)ctx;
    // Network workers are joined before the first check. The input owner is the
    // only remaining spool writer, so reuse that scan at the locked recheck.
    if (!s_sleep_files_checked) {
        s_sleep_files_safe = spool_sleep_safe();
        s_sleep_files_checked = true;
    }
    return !memo_recorder_is_busy() && !memo_player_is_busy() &&
           s_handoff.phase == MEMO_HANDOFF_NORMAL &&
           s_sleep_files_safe && !memo_spool_recovery_blocked() && uxQueueMessagesWaiting(s_queue) == 0 &&
           bsp_button_read_mv() >= BSP_BTN_MV_RELEASED_MIN;
}
static int power_stop_player(void *ctx) { (void)ctx; return memo_player_stop(); }
static bool power_lock(void *ctx) { (void)ctx; return bsp_lvgl_lock(1000); }
static void power_unlock(void *ctx) { (void)ctx; bsp_lvgl_unlock(); }
static int power_arm(void *ctx)
{
    (void)ctx;
    return esp_deep_sleep_enable_gpio_wakeup(1ULL << BSP_BTN_GPIO, ESP_GPIO_WAKEUP_GPIO_LOW);
}
static void power_recover(void *ctx)
{
    (void)ctx;
    s_power.activity_ms = esp_timer_get_time() / 1000;
    s_network_resume_needed = true;
}
static void power_release_network(void *ctx)
{
    (void)ctx;
    s_ui_ready = false;
    esp_timer_stop(s_arm_timer);
    memo_discovery_stop();
    const esp_err_t err = memo_net_suspend();
    if (err != ESP_OK) ESP_LOGW(TAG, "terminal WiFi release: %s", esp_err_to_name(err));
}
static int power_buttons(void *ctx)
{
    (void)ctx;
    int level = 0;
    const esp_err_t err = bsp_button_prepare_deep_sleep(&level);
    if (err != ESP_OK) ESP_LOGE(TAG, "button shutdown failed: %s", esp_err_to_name(err));
    // A press after the preflight is allowed to wake immediately, never be lost.
    return err;
}
static int power_warning(const char *step, esp_err_t err)
{
    if (err != ESP_OK) ESP_LOGW(TAG, "%s: %s", step, esp_err_to_name(err));
    return err;
}
static int power_battery(void *ctx) { (void)ctx; return power_warning("battery sleep", bsp_battery_sleep()); }
static int power_audio(void *ctx) { (void)ctx; return power_warning("audio sleep", bsp_audio_sleep()); }
static int power_audio_pins(void *ctx) { (void)ctx; return power_warning("audio pins", bsp_audio_prepare_deep_sleep()); }
static int power_i2c_pins(void *ctx) { (void)ctx; return power_warning("I2C pins", bsp_i2c_prepare_deep_sleep()); }
static int power_display(void *ctx) { (void)ctx; return power_warning("display sleep", bsp_display_prepare_deep_sleep()); }
static void power_sleep(void *ctx) { (void)ctx; esp_deep_sleep_start(); }
static void power_restart(void *ctx) { (void)ctx; esp_restart(); }
static const memo_power_ops_t s_power_ops = {
    .stop_events = handoff_stop_events, .stop_upload = handoff_stop_upload,
    .stop_player = power_stop_player, .safe = power_safe,
    .lock_display = power_lock, .unlock_display = power_unlock,
    .arm_wake = power_arm, .recover_workers = power_recover,
    .release_network = power_release_network, .release_buttons = power_buttons,
    .battery_sleep = power_battery, .audio_sleep = power_audio,
    .audio_pins = power_audio_pins, .i2c_pins = power_i2c_pins,
    .display_sleep = power_display, .sleep = power_sleep, .restart = power_restart,
};

static void power_poll(int64_t now_ms)
{
    memo_player_poll();
    if (s_local_playback_held && !memo_player_is_busy()) {
        memo_upload_set_playback_busy(false); s_local_playback_held = false;
    }
    const bool keep_on = memo_state_keeps_screen_on(s_sm.state) ||
                         s_settings_edit.active ||
                         memo_recorder_is_busy() || memo_player_is_active() ||
                         s_handoff.phase != MEMO_HANDOFF_NORMAL ||
                         bsp_button_read_mv() < BSP_BTN_MV_RELEASED_MIN;
    const memo_settings_t *settings = memo_settings_get();
    const memo_power_action_t action = memo_power_poll(&s_power, now_ms,
        settings->screen_timeout_s, settings->sleep_min, keep_on,
        memo_spool_recovery_blocked() || memo_player_is_busy() || s_network_resume_needed);
    const bool on = action == MEMO_POWER_ON;
    if (on != s_backlight_on) {
        bsp_display_backlight(on ? 100 : 0);
        s_backlight_on = on;
    }
    if (action == MEMO_POWER_SLEEP) {
        s_sleep_files_checked = false;
        memo_power_shutdown(&s_power_ops);
    }
}

static void poll_draft(void)
{
    if (s_sm.state != MEMO_STATE_LIST || memo_spool_recovery_blocked()) return;
    memo_spool_entry_t entries[MEMO_SPOOL_MAX_ENTRIES];
    const int count = memo_spool_scan(entries, MEMO_SPOOL_MAX_ENTRIES);
    for (int i = 0; i < count && i < MEMO_SPOOL_MAX_ENTRIES; i++) {
        if (entries[i].state != MEMO_SPOOL_DRAFT) continue;
        memo_upload_set_capture_busy(true);
        snprintf(s_last_id, sizeof(s_last_id), "%s", entries[i].id);
        s_sm.state = MEMO_STATE_CONFIRM;
        s_sm.confirm_save = true;
        s_confirm_ms = entries[i].duration_ms;
        s_capture_hint = "恢复录音 · 请选择";
        reconcile_ui();
        return;
    }
}

static UBaseType_t s_input_stack_low = (UBaseType_t)-1;
static void log_input_stack_low(void)
{
    const UBaseType_t free_bytes = uxTaskGetStackHighWaterMark(NULL);
    if (free_bytes < s_input_stack_low) {
        s_input_stack_low = free_bytes;
        ESP_LOGI(TAG, "memo_input stack high-water: %u bytes free", (unsigned)free_bytes);
    }
}

static void input_task(void *arg)
{
    (void)arg;

    int64_t next_tick = 0;
    for (;;) {
        input_event_t event;
        const bool received = xQueueReceive(s_queue, &event, pdMS_TO_TICKS(50)) == pdTRUE;
        if (!s_ui_ready) continue;
        const int64_t now = esp_timer_get_time();
        bool consumed = false;
        const bool detail_switching = s_detail_navigation.pending;
        if (received && event.physical) {
            const bool was_blank = s_power.blank;
            consumed = memo_power_key(&s_power, now / 1000, event.press, event.release);
            // An intentional held OK may span backlight wake. A short gesture
            // still cancels arming and its trailing click remains consumed.
            if (was_blank && s_sm.state == MEMO_STATE_LIST && event.input == MEMO_IN_OK_PRESS) {
                s_wake_capture = true;
                consumed = false;
            } else if (s_wake_capture && event.input == MEMO_IN_OK_RELEASE) {
                s_wake_capture = false;
                consumed = false;
            }
            if (!s_backlight_on) {
                bsp_display_backlight(100);
                s_backlight_on = true;
            }
        }
        if (received && !consumed) {
            if (event.input == MEMO_IN_OK_PRESS && s_sm.state != MEMO_STATE_ARMING &&
                s_sm.state != MEMO_STATE_RECORDING) {
                s_input_release_epoch = event.release_epoch;
                memo_capture_press(&s_capture_hold, event.at_us, s_sm.state == MEMO_STATE_LIST);
                if (detail_switching) memo_capture_commit(&s_capture_hold);
            }
            else if (event.input == MEMO_IN_OK_RELEASE)
                memo_capture_release(&s_capture_hold, event.at_us);
        }
        if (now >= next_tick) {
            next_tick = now + 200000;
            tick_timer_cb(NULL);
            poll_browse();
            poll_draft();
            reconcile_ui();
            poll_rejected_choice();
            power_poll(esp_timer_get_time() / 1000);
            log_input_stack_low();
        }
        if (!received || consumed || event.input == MEMO_IN_COUNT) {
            if (s_sm.state == MEMO_STATE_ARMING) reconcile_ui();
            poll_capture_prepare();
            continue;
        }
        if (event.input == MEMO_IN_OK_CLICK && s_capture_hold.consume_click) continue;
        if (detail_switching && (event.input == MEMO_IN_OK_CLICK ||
            event.input == MEMO_IN_UP_HOLD || event.input == MEMO_IN_DOWN_HOLD)) continue;
        if (event.input == MEMO_IN_ARM_COMPLETE) {
            // A completion already queued by an earlier cancelled gesture must
            // not complete a newly pressed gesture ahead of its own deadline.
            if (s_sm.state != MEMO_STATE_ARMING ||
                now - s_arming_started < (int64_t)MEMO_ARMING_MS * 1000) continue;
            memo_capture_commit(&s_capture_hold);
        }
        const memo_input_t input = event.input;
        if (s_sm.state == MEMO_STATE_UPLOAD_REJECTED && s_reject_deciding) continue;

        const memo_state_t before = s_sm.state;
        memo_action_t action = memo_sm_handle(&s_sm, input);
        if (action != MEMO_ACT_NONE) {
            ESP_LOGI(TAG, "%s + %s -> %s (now %s)", memo_state_name(before),
                     memo_input_name(input), memo_action_name(action),
                     memo_state_name(s_sm.state));
            execute(action);
            log_input_stack_low();
        }
        reconcile_ui();
        poll_capture_prepare();
        poll_rejected_choice();
    }
}

static bool init_core(void)
{
    memo_sm_init(&s_sm);
    if (memo_api_init() != ESP_OK) {
        ESP_LOGE(TAG, "cannot initialise metadata cache");
        return false;
    }

    ESP_LOGI(TAG, "heap after boot: %u", (unsigned)esp_get_free_heap_size());

    if (memo_settings_init() != ESP_OK) {
        ESP_LOGW(TAG, "settings unavailable; using defaults");
    }

    const uint32_t wake_causes = esp_sleep_get_wakeup_causes();
    if (wake_causes & (1u << ESP_SLEEP_WAKEUP_GPIO)) {
        key_guard_arm(&s_guard, esp_timer_get_time(), 10LL * 1000 * 1000);
        ESP_LOGI(TAG, "woken by key: swallow input until it is released");
    }

    s_queue = xQueueCreate(8, sizeof(input_event_t));
    if (s_queue == NULL) {
        ESP_LOGE(TAG, "cannot create input queue");
        return false;
    }
    if (xTaskCreate(input_task, "memo_input", 8192, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "cannot create input task");
        return false;
    }

    const esp_timer_create_args_t arm_args = {
        .callback = arm_timer_cb, .name = "memo_arm",
    };
    if (esp_timer_create(&arm_args, &s_arm_timer) != ESP_OK) {
        ESP_LOGE(TAG, "cannot create arming timer");
        return false;
    }
    return true;
}

static bool init_display(void)
{
    esp_err_t i2c_err = bsp_i2c_init();
    if (i2c_err != ESP_OK) {
        ESP_LOGW(TAG, "shared I2C unavailable: %s", esp_err_to_name(i2c_err));
    }

    if (bsp_display_init() != ESP_OK || bsp_lvgl_init() == NULL) {
        ESP_LOGE(TAG, "display/LVGL init failed; check SPI pins");
        return false;
    }
    bsp_display_backlight(100);
    return true;
}

static void init_storage(void)
{
    /* 列表页要显示待上传条数与剩余空间，所以 spool 必须在建页面之前挂好。 */
    ESP_LOGI(TAG, "heap after display/LVGL: %u", (unsigned)esp_get_free_heap_size());
    if (memo_spool_mount() != ESP_OK) {
        ESP_LOGE(TAG, "spool mount failed; recordings will not persist");
    } else {
        const int dropped = memo_spool_recover();
        if (dropped > 0) {
            ESP_LOGW(TAG, "recovered/quarantined %d interrupted recording(s)", dropped);
        }
    }

    load_list_model();
}

static void init_pages(void)
{
    if (bsp_lvgl_lock(1000)) {
        prepare_page(PAGE_LIST);
        refresh_list();
        lv_screen_load(s_list_screen);
        bsp_lvgl_unlock();
    }
}

static void init_audio(void)
{
    /* 录音通路依赖 codec：不先 init，bsp_audio_set_format 会直接返回
       ESP_ERR_INVALID_STATE，表现就是"按了没反应"。 */
    ESP_LOGI(TAG, "heap after spool: %u", (unsigned)esp_get_free_heap_size());
    if (bsp_audio_init() != ESP_OK) {
        ESP_LOGE(TAG, "audio codec unavailable; recording will not work");
    } else {
        /* BSP 的上电音量是 0。不给一个默认值，录音能存、播放"成功"但全程无声。 */
        bsp_audio_set_volume(memo_settings_get()->volume);
    }
    if (memo_recorder_init() != ESP_OK) {
        ESP_LOGE(TAG, "recorder unavailable; recording will not work");
    }
    if (memo_player_init() != ESP_OK) {
        ESP_LOGE(TAG, "player unavailable; playback will not work");
    }
    bsp_battery_init();
}

static void init_network(void)
{
    ESP_LOGI(TAG, "heap after audio/battery: %u", (unsigned)esp_get_free_heap_size());
    /* 有凭据就自己连上；没有则等用户从设置里配网。 */
    if (memo_net_start() != ESP_OK) {
        ESP_LOGW(TAG, "network start failed");
    }
    if (memo_discovery_init() != ESP_OK) ESP_LOGW(TAG, "service discovery initialisation failed");
    /* 常驻的网络 worker：空闲时拉列表，有待上传就上传。配网会让它让位。 */
    ESP_LOGI(TAG, "heap after wifi: %u (largest %u)",
             (unsigned)esp_get_free_heap_size(),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));

    if (memo_upload_start() != ESP_OK) {
        ESP_LOGW(TAG, "uploader start failed");
    }
    ESP_LOGI(TAG, "heap after uploader: %u (largest %u)",
             (unsigned)esp_get_free_heap_size(),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));

    if (memo_events_start() != ESP_OK) {
        ESP_LOGW(TAG, "event subscriber start failed");
    }
    ESP_LOGI(TAG, "heap after subscriber: %u (largest %u)",
             (unsigned)esp_get_free_heap_size(),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
}

static void init_buttons(void)
{
    if (bsp_button_init(on_key, NULL) != ESP_OK) {
        ESP_LOGE(TAG, "button init failed");
    } else {
        /* 唤醒键可能在按键驱动起来前就松了手，按真实电压补一次同步。 */
        const bool released = bsp_button_read_mv() >= BSP_BTN_MV_RELEASED_MIN;
        key_guard_sync_released(&s_guard, released);
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "voice memo starting");

    if (!init_core()) return;
    if (!init_display()) return;
    init_storage();
    init_audio();
    init_network();
    init_buttons();
    /* Allocate long-lived platform/tasks/locks before any short-lived page. */
    init_pages();

    ESP_LOGI(TAG, "idle; free heap %u (largest block %u)",
             (unsigned)esp_get_free_heap_size(),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    s_power.activity_ms = esp_timer_get_time() / 1000;
    s_ui_ready = true;
    if (s_guard.armed) {
        const int mv = bsp_button_read_mv();
        const int windows[][2] = BSP_BTN_MV_TABLE;
        if (mv >= windows[BSP_BTN_OK][0] && mv < windows[BSP_BTN_OK][1])
            enqueue_input(MEMO_IN_OK_PRESS);
    }
}
