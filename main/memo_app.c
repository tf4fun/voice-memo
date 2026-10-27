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
#include "memo_recorder.h"
#include "memo_spool.h"
#include "memo_font.h"
#include "memo_ui_common.h"
#include "memo_ui_detail.h"
#include "memo_player.h"
#include "memo_settings.h"
#include "memo_api.h"
#include "memo_discovery.h"
#include "memo_events.h"
#include "memo_net.h"
#include "memo_provision.h"
#include "memo_ui_provision.h"
#include "memo_ui_settings.h"
#include "memo_upload.h"
#include "memo_rejection_layout.h"
#include "memo_state.h"
#include "memo_time.h"

static const char *TAG = "memo_app";

/* ---------- 视觉变量（docs/design/visual-language.zh_CN.md） ---------- */
#define VM_PAPER 0xF6F1E6
#define VM_CARD  0xFFFFFF
#define VM_INK   0x141210
#define VM_MUTED 0x7A7368
#define VM_SUN   0xFFC53D
#define VM_CORAL 0xE5483F

#define VM_RADIUS_CARD 12
#define VM_STROKE      2
#define VM_SHADOW      3
#define VM_INSET       8
#define VM_LEVEL_BARS  12

/* ---------- 输入分发 ---------- */
static QueueHandle_t s_queue; /* 队列元素是 memo_input_t */
static memo_sm_t s_sm;
static key_guard_t s_guard;

static esp_timer_handle_t s_arm_timer;

/* ---------- 页面对象 ---------- */
static lv_obj_t *s_list_screen;
static lv_obj_t *s_record_screen;
static lv_obj_t *s_list_status;
static lv_obj_t *s_list_hint;
static lv_obj_t *s_timer_label;
static lv_obj_t *s_record_hint;
static lv_obj_t *s_arm_arc;
static lv_obj_t *s_level_bars[VM_LEVEL_BARS];
static lv_obj_t *s_sheet;
static lv_obj_t *s_sheet_shadow;
static lv_obj_t *s_opt_save;
static lv_obj_t *s_opt_cancel;
static char s_last_id[37];
static uint32_t s_detail_duration_ms;  /* 详情页当前条目的时长，播放器未启动时用它 */
static bool s_network_resume_needed;
static bool s_rec_network_suspended;   /* 本次录音是否为了腾内存让出过网络 */
static lv_obj_t *s_reject_sheet;
static lv_obj_t *s_reject_shadow;
static lv_obj_t *s_reject_message;
static lv_obj_t *s_reject_retry;
static lv_obj_t *s_reject_delete;
static char s_reject_id[37];
static bool s_reject_deciding;

/* ---------- 小工具 ---------- */
static lv_obj_t *card_create(lv_obj_t *parent, int x, int y, int w, int h)
{
    return memo_ui_card(parent, x, y, w, h, NULL);
}

static void enqueue_input(memo_input_t input);

static void arc_anim_cb(void *obj, int32_t value)
{
    lv_arc_set_value((lv_obj_t *)obj, value);
}

static void arm_ring_start(void)
{
    if (s_arm_arc == NULL) return;
    lv_obj_clear_flag(s_arm_arc, LV_OBJ_FLAG_HIDDEN);
    lv_arc_set_value(s_arm_arc, 0);

    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, s_arm_arc);
    /* 弧的值域是 0..100；把毫秒直接当值会瞬间顶满，看起来像没有动画。 */
    lv_anim_set_values(&anim, 0, 100);
    lv_anim_set_duration(&anim, MEMO_ARMING_MS);
    lv_anim_set_exec_cb(&anim, arc_anim_cb);
    lv_anim_start(&anim);
}

static void arm_ring_hide(void)
{
    if (s_arm_arc == NULL) return;
    lv_anim_delete(s_arm_arc, arc_anim_cb);
    lv_obj_add_flag(s_arm_arc, LV_OBJ_FLAG_HIDDEN);
}

static void arm_timer_cb(void *arg)
{
    (void)arg;
    enqueue_input(MEMO_IN_ARM_COMPLETE);
}

static bool s_warned;

static void refresh_list(void);

static void tick_timer_cb(void *arg)
{
    (void)arg;
    if (s_network_resume_needed && !s_rec_network_suspended && s_sm.state != MEMO_STATE_PROVISIONING) {
        const esp_err_t upload = memo_upload_start();
        const esp_err_t events = memo_events_start();
        if (upload == ESP_OK && events == ESP_OK) s_network_resume_needed = false;
    }
    if (s_sm.state == MEMO_STATE_UPLOAD_REJECTED) return;

    /* 设置页：让"服务器"行能跟上发现进度。 */
    if (s_sm.state == MEMO_STATE_SETTINGS || s_sm.state == MEMO_STATE_SUB_PAGE) {
        static uint32_t refresh_div;
        if (++refresh_div >= 5) { /* 1 秒 */
            refresh_div = 0;
            if (bsp_lvgl_lock(30)) {
                memo_ui_settings_refresh_values();
                bsp_lvgl_unlock();
            }
        }
    }

    /* 列表页：服务端数据更新了就重绘一次。 */
    if (s_sm.state == MEMO_STATE_LIST && memo_api_take_updated()) {
        if (bsp_lvgl_lock(50)) {
            refresh_list();
            bsp_lvgl_unlock();
        }
        return;
    }

    if (s_sm.state == MEMO_STATE_PROVISIONING) {
        if (bsp_lvgl_lock(50)) {
            memo_ui_provision_refresh();
            bsp_lvgl_unlock();
        }
        return;
    }

    /* 详情页：刷新播放条，并在播放自然结束时回到"已展开"。 */
    if (!memo_state_is_recording(s_sm.state) && s_sm.state != MEMO_STATE_ARMING) {
        const bool playing = memo_player_is_active();
        /* 没在播放时播放器报告的时长是 0；此时必须沿用条目自己的时长，
           否则播放条会被刷成 "0:00 / 0:00"。 */
        const uint32_t duration = playing ? memo_player_duration_ms() : s_detail_duration_ms;
        if (bsp_lvgl_lock(50)) {
            memo_ui_detail_set_playback(playing, memo_player_position_ms(), duration);
            bsp_lvgl_unlock();
        }
        if (!playing && s_sm.state == MEMO_STATE_PLAYING) {
            s_sm.state = MEMO_STATE_DETAIL_EXPANDED;
        }
        return;
    }

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
    if (!bsp_lvgl_lock(50)) return;
    if (s_timer_label != NULL) {
        char text[16];
        if (memo_time_timer(memo_recorder_elapsed_ms(), text, sizeof(text))) {
            lv_label_set_text(s_timer_label, text);
        }
        if (memo_recorder_elapsed_ms() >= MEMO_RECORD_WARN_MS) {
            lv_obj_set_style_text_color(s_timer_label, lv_color_hex(VM_CORAL), 0);
        }
    }
    uint8_t level = memo_recorder_level();
    int lit = (int)level * VM_LEVEL_BARS / 100;
    for (int i = 0; i < VM_LEVEL_BARS; i++) {
        if (s_level_bars[i] == NULL) continue;
        lv_obj_set_style_bg_color(s_level_bars[i],
                                  lv_color_hex(i < lit ? VM_INK : VM_PAPER), 0);
    }
    bsp_lvgl_unlock();
}

/* ---------- 页面构建 ---------- */
#define LIST_ROWS 5

static lv_obj_t *s_rows[LIST_ROWS];
static lv_obj_t *s_row_shadows[LIST_ROWS];
static lv_obj_t *s_row_time[LIST_ROWS];
static lv_obj_t *s_row_text[LIST_ROWS];
static lv_obj_t *s_row_dur[LIST_ROWS];
static lv_obj_t *s_battery;
/* 一屏列表的统一视图：先是本地待上传，然后是服务端返回的历史。 */
typedef struct {
    bool local;      /* true = 本地待上传文件 */
    memo_spool_state_t spool_state;
    char id[37];
    int64_t recorded_at;
    uint32_t duration_ms;
    char text[80];
} list_view_t;

static list_view_t s_view[MEMO_API_MAX_ITEMS + 1];
static int s_view_count;
static int s_selected;

static void build_list_page(void)
{
    s_list_screen = lv_obj_create(NULL);
    memo_ui_flat_rect(s_list_screen, VM_PAPER, 0);
    lv_obj_set_style_radius(s_list_screen, 0, 0);

    const int width = lv_display_get_horizontal_resolution(NULL);

    lv_obj_t *title = memo_ui_label(s_list_screen, memo_font_label(), VM_INK);
    lv_label_set_text(title, "备忘");
    lv_obj_set_pos(title, VM_INSET + 4, VM_INSET + 4);

    s_battery = memo_ui_label(s_list_screen, &lv_font_montserrat_14, VM_MUTED);
    lv_label_set_text(s_battery, "--");
    lv_obj_align(s_battery, LV_ALIGN_TOP_RIGHT, -VM_INSET - 4, VM_INSET + 6);

    lv_obj_t *divider = lv_obj_create(s_list_screen);
    memo_ui_flat_rect(divider, VM_INK, 0);
    lv_obj_set_size(divider, width - 2 * VM_INSET, 2);
    lv_obj_set_pos(divider, VM_INSET, 34);

    for (int i = 0; i < LIST_ROWS; i++) {
        lv_obj_t *row = memo_ui_card(s_list_screen, VM_INSET, 44 + i * 46,
                                     width - 2 * VM_INSET - VM_SHADOW, 40,
                                     &s_row_shadows[i]);
        s_rows[i] = row;

        s_row_time[i] = memo_ui_label(row, &lv_font_montserrat_14, VM_MUTED);
        lv_obj_align(s_row_time[i], LV_ALIGN_LEFT_MID, 8, 0);

        s_row_text[i] = memo_ui_label(row, memo_font_label(), VM_INK);
        lv_obj_align(s_row_text[i], LV_ALIGN_LEFT_MID, 52, 0);

        s_row_dur[i] = memo_ui_label(row, &lv_font_montserrat_14, VM_MUTED);
        lv_obj_align(s_row_dur[i], LV_ALIGN_RIGHT_MID, -8, 0);
    }

    s_list_hint = memo_ui_label(s_list_screen, memo_font_label(), VM_MUTED);
    lv_label_set_text(s_list_hint, "长按 OK 录音");
    lv_obj_align(s_list_hint, LV_ALIGN_BOTTOM_LEFT, VM_INSET + 4, -VM_INSET - 2);

    s_list_status = memo_ui_label(s_list_screen, memo_font_label(), VM_MUTED);
    lv_label_set_text(s_list_status, "");
    lv_obj_align(s_list_status, LV_ALIGN_BOTTOM_RIGHT, -VM_INSET - 4, -VM_INSET - 2);
}

static void build_record_page(void)
{
    s_record_screen = lv_obj_create(NULL);
    memo_ui_flat_rect(s_record_screen, VM_PAPER, 0);
    lv_obj_set_style_radius(s_record_screen, 0, 0);

    lv_obj_t *dot = lv_obj_create(s_record_screen);
    memo_ui_flat_rect(dot, VM_CORAL, LV_RADIUS_CIRCLE);
    lv_obj_set_size(dot, 12, 12);
    lv_obj_set_pos(dot, VM_INSET + 6, VM_INSET + 10);

    lv_obj_t *rec = memo_ui_label(s_record_screen, &lv_font_montserrat_14, VM_INK);
    lv_label_set_text(rec, "REC");
    lv_obj_set_pos(rec, VM_INSET + 26, VM_INSET + 6);

    s_timer_label = memo_ui_label(s_record_screen, &lv_font_montserrat_40, VM_INK);
    lv_label_set_text(s_timer_label, "00:00");
    lv_obj_align(s_timer_label, LV_ALIGN_TOP_MID, 0, 70);

    /* 十二根离散电平条，读起来像贴纸，也便宜。 */
    const int bar_w = 12, gap = 4;
    const int total = VM_LEVEL_BARS * bar_w + (VM_LEVEL_BARS - 1) * gap;
    int x = (lv_display_get_horizontal_resolution(NULL) - total) / 2;
    for (int i = 0; i < VM_LEVEL_BARS; i++) {
        lv_obj_t *bar = lv_obj_create(s_record_screen);
        memo_ui_flat_rect(bar, VM_PAPER, 3);
        lv_obj_set_style_border_color(bar, lv_color_hex(VM_INK), 0);
        lv_obj_set_style_border_width(bar, 1, 0);
        lv_obj_set_size(bar, bar_w, 20);
        lv_obj_set_pos(bar, x + i * (bar_w + gap), 190);
        s_level_bars[i] = bar;
    }

    /* 待发阶段的圆环：按下即开始填充，填满才进入录音。
       它必须真的被创建——之前这段在某次重写中丢失，导致按下毫无反馈。 */
    s_arm_arc = lv_arc_create(s_record_screen);
    lv_obj_remove_style_all(s_arm_arc);
    lv_obj_set_size(s_arm_arc, 132, 132);
    lv_obj_align(s_arm_arc, LV_ALIGN_TOP_MID, 0, 46);
    lv_arc_set_range(s_arm_arc, 0, 100);
    lv_arc_set_bg_angles(s_arm_arc, 0, 360);
    lv_arc_set_value(s_arm_arc, 0);
    lv_obj_set_style_arc_width(s_arm_arc, 10, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_arm_arc, 10, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_arm_arc, lv_color_hex(VM_SUN), LV_PART_MAIN);
    lv_obj_set_style_arc_opa(s_arm_arc, LV_OPA_20, LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_arm_arc, lv_color_hex(VM_CORAL), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_arm_arc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_clear_flag(s_arm_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_arm_arc, LV_OBJ_FLAG_HIDDEN);

    s_record_hint = memo_ui_label(s_record_screen, &lv_font_montserrat_14, VM_MUTED);
    lv_label_set_text(s_record_hint, "release to finish");
    lv_obj_align(s_record_hint, LV_ALIGN_BOTTOM_MID, 0, -VM_INSET - 6);

    /* 松手后弹出的选择条：保存 / 取消 */
    const int width = lv_display_get_horizontal_resolution(NULL);
    s_sheet = memo_ui_card(s_record_screen, VM_INSET + 12, 210,
                           width - 2 * VM_INSET - 24 - VM_SHADOW, 84,
                           &s_sheet_shadow);

    s_opt_save = memo_ui_label(s_sheet, &lv_font_montserrat_20, VM_INK);
    lv_label_set_text(s_opt_save, "save");
    lv_obj_align(s_opt_save, LV_ALIGN_TOP_MID, 0, 10);

    s_opt_cancel = memo_ui_label(s_sheet, &lv_font_montserrat_20, VM_MUTED);
    lv_label_set_text(s_opt_cancel, "cancel");
    lv_obj_align(s_opt_cancel, LV_ALIGN_TOP_MID, 0, 44);
}

/* 卡片和它的投影是两个对象，必须一起藏，否则页面上会留下一个黑方块。 */
static void sheet_visible(bool visible)
{
    lv_obj_t *objects[] = {s_sheet, s_sheet_shadow};
    for (size_t i = 0; i < sizeof(objects) / sizeof(objects[0]); i++) {
        if (objects[i] == NULL) continue;
        if (visible) {
            lv_obj_clear_flag(objects[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(objects[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void refresh_confirm_sheet(void)
{
    if (s_sheet == NULL) return;
    const bool save = s_sm.confirm_save;
    lv_obj_set_style_text_color(s_opt_save, lv_color_hex(save ? VM_INK : VM_MUTED), 0);
    lv_obj_set_style_text_color(s_opt_cancel, lv_color_hex(save ? VM_MUTED : VM_INK), 0);
    lv_obj_set_style_text_decor(s_opt_save, save ? LV_TEXT_DECOR_UNDERLINE : LV_TEXT_DECOR_NONE, 0);
    lv_obj_set_style_text_decor(s_opt_cancel, save ? LV_TEXT_DECOR_NONE : LV_TEXT_DECOR_UNDERLINE, 0);
}

static void row_highlight(int index, bool selected)
{
    if (index < 0 || index >= LIST_ROWS) return;
    lv_obj_t *row = s_rows[index];
    lv_obj_t *shadow = s_row_shadows[index];
    if (row == NULL) return;

    lv_obj_set_style_bg_color(row, lv_color_hex(selected ? VM_SUN : VM_CARD), 0);
    lv_obj_set_style_translate_y(row, selected ? -2 : 0, 0);
    if (shadow != NULL) {
        lv_obj_set_style_translate_y(shadow, selected ? -1 : 0, 0);
    }
}

/* 组装列表视图并重画。本地待上传排在最前，其后是服务端历史。 */
static void refresh_list(void)
{
    s_view_count = 0;

    memo_spool_entry_t entries[MEMO_SPOOL_MAX_ENTRIES];
    const int local = memo_spool_scan(entries, MEMO_SPOOL_MAX_ENTRIES);
    for (int i = 0; i < local && i < MEMO_SPOOL_MAX_ENTRIES; i++) {
        if (entries[i].state == MEMO_SPOOL_SENT || entries[i].state == MEMO_SPOOL_RECORDING ||
            entries[i].state == MEMO_SPOOL_DRAFT) continue;
        if (s_view_count >= MEMO_API_MAX_ITEMS + 1) break;
        list_view_t *row = &s_view[s_view_count++];
        row->local = true;
        row->spool_state = entries[i].state;
        strncpy(row->id, entries[i].id, sizeof(row->id) - 1);
        row->recorded_at = (int64_t)entries[i].recorded_at;
        row->duration_ms = entries[i].duration_ms;
        snprintf(row->text, sizeof(row->text), "%s",
                 entries[i].state == MEMO_SPOOL_LEGACY_KEPT ? "待重试" :
                 entries[i].state == MEMO_SPOOL_BAD ? "上传失败" : "待上传");
    }

    const int remote = memo_api_count();
    for (int i = 0; i < remote && s_view_count < MEMO_API_MAX_ITEMS + 1; i++) {
        const memo_api_item_t *item = memo_api_item(i);
        if (item == NULL) continue;
        list_view_t *row = &s_view[s_view_count++];
        row->local = false;
        strncpy(row->id, item->id, sizeof(row->id) - 1);
        row->recorded_at = item->recorded_at;
        row->duration_ms = item->duration_ms;
        if (item->snippet[0] != '\0') {
            snprintf(row->text, sizeof(row->text), "%s", item->snippet);
        } else if (strcmp(item->status, "ready") == 0) {
            snprintf(row->text, sizeof(row->text), "无转写");
        } else {
            snprintf(row->text, sizeof(row->text), "转写中");
        }
    }

    if (s_selected >= s_view_count) s_selected = s_view_count > 0 ? s_view_count - 1 : 0;
    if (s_selected < 0) s_selected = 0;

    const int32_t tz = memo_settings_get()->tz_offset_min;

    for (int i = 0; i < LIST_ROWS; i++) {
        const bool used = i < s_view_count;
        memo_ui_card_visible(s_rows[i], s_row_shadows[i], used);
        if (!used) continue;

        char clock[8] = "--:--";
        char duration[16] = "0:00";
        if (s_view[i].recorded_at > 0) {
            memo_time_clock(s_view[i].recorded_at, tz, clock, sizeof(clock));
            memo_time_duration((int64_t)s_view[i].duration_ms, duration, sizeof(duration));
        }
        lv_label_set_text(s_row_time[i], clock);
        lv_label_set_text(s_row_text[i], s_view[i].text);
        lv_label_set_text(s_row_dur[i], duration);
        row_highlight(i, i == s_selected);
    }

    char status[32];
    if (s_view_count == 0) {
        snprintf(status, sizeof(status), "还没有录音");
    } else if (memo_api_count() == 0 && memo_net_has_ip()) {
        snprintf(status, sizeof(status), "%d 条", s_view_count);
    } else {
        snprintf(status, sizeof(status), "%d 条", s_view_count);
    }
    if (memo_spool_recovery_blocked()) {
        lv_label_set_text(s_list_hint, "storage error - restart");
        status[0] = '\0';
    }
    lv_label_set_text(s_list_status, status);
    lv_obj_set_style_text_color(s_list_status, lv_color_hex(VM_MUTED), 0);

    int soc = bsp_battery_soc();
    if (soc < 0) {
        lv_label_set_text(s_battery, "--");
    } else {
        char buf[12];
        snprintf(buf, sizeof(buf), "%d%%", soc);
        lv_label_set_text(s_battery, buf);
    }

    ESP_LOGI(TAG, "list: %d row(s) (%d local, %d remote), free=%u KB", s_view_count,
             local, memo_api_count(), (unsigned)(memo_spool_free_bytes() / 1024));
}

/* 播放当前选中项：本地文件直接播，服务端条目先下载到 SPIFFS 的临时文件。 */
static void play_selected(void)
{
    if (s_selected >= s_view_count) return;
    const list_view_t *row = &s_view[s_selected];

    if (memo_player_is_active()) memo_player_stop();

    if (row->local) {
        const char *ext = row->spool_state == MEMO_SPOOL_LEGACY_KEPT ? "keep" :
                          row->spool_state == MEMO_SPOOL_BAD ? "bad" : "memo";
        char path[80];
        snprintf(path, sizeof(path), "%s/%s.%s", MEMO_SPOOL_MOUNT_POINT, row->id, ext);
        if (memo_player_play_file(path) == ESP_OK) {
            ESP_LOGI(TAG, "playing local %s", row->id);
        } else {
            ESP_LOGE(TAG, "play failed");
        }
        return;
    }

    /* 下载在播放任务内部完成：按键任务不能在这里阻塞，否则整个输入队列会停。 */
    if (memo_player_play_remote(row->id) == ESP_OK) {
        ESP_LOGI(TAG, "streaming %s from server", row->id);
    } else {
        ESP_LOGE(TAG, "remote play failed");
    }
}

/* ---------- 动作执行 ---------- */
static void show_screen(lv_obj_t *screen)
{
    if (screen != NULL) lv_screen_load(screen);
}

static void execute(memo_action_t action)
{
    switch (action) {
    case MEMO_ACT_START_ARMING:
        if (memo_upload_pending()) {
            /* 局部存储是"处理中"状态：上一条没走完就不开始新的一条。 */
            ESP_LOGW(TAG, "recording blocked: previous memo is still being uploaded");
            if (bsp_lvgl_lock(150)) {
                if (memo_spool_recovery_blocked()) refresh_list();
                else lv_label_set_text(s_list_status, "上一条待上传");
                lv_obj_set_style_text_color(s_list_status, lv_color_hex(VM_CORAL), 0);
                bsp_lvgl_unlock();
            }
            s_sm.state = MEMO_STATE_LIST;
            break;
        }
        if (bsp_lvgl_lock(200)) {
            if (s_timer_label != NULL) lv_label_set_text(s_timer_label, "00:00");
            show_screen(s_record_screen); /* 圆环在这个页面上，待发时就切过来 */
            arm_ring_start();
            bsp_lvgl_unlock();
        }
        esp_timer_stop(s_arm_timer);
        esp_timer_start_once(s_arm_timer, (uint64_t)MEMO_ARMING_MS * 1000);
        break;

    case MEMO_ACT_CANCEL_ARMING:
        esp_timer_stop(s_arm_timer);
        if (bsp_lvgl_lock(100)) {
            arm_ring_hide();
            show_screen(s_list_screen);
            bsp_lvgl_unlock();
        }
        break;

    case MEMO_ACT_BEGIN_RECORDING: {
        memo_upload_set_capture_busy(true);
        char id[37];
        uint8_t random[16];
        esp_fill_random(random, sizeof(random));
        memo_container_make_uuid(id, random);

        ESP_LOGI(TAG, "recording start: free heap %u (largest block %u)",
                 (unsigned)esp_get_free_heap_size(),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
        /* 录音要 28 KB 连续栈 + 24.6 KB 编码器。网络常驻后通常仍有余量，
           先直接开；实在不够才让网络暂时让位（并记下来，停止后还要还回去）。 */
        esp_err_t err = memo_recorder_start(id);
        if (err == ESP_ERR_NO_MEM) {
            ESP_LOGW(TAG, "no room for the recorder, standing the network down");
            esp_err_t events_stop = memo_events_stop();
            esp_err_t upload_stop = memo_upload_stop();
            if (events_stop == ESP_OK && upload_stop == ESP_OK) {
                memo_net_suspend();
                s_rec_network_suspended = true;
                err = memo_recorder_start(id);
            } else {
                ESP_LOGW(TAG, "network still owned by worker; capture retry deferred");
                s_network_resume_needed = true;
                memo_events_start();
                memo_upload_start();
            }
        }

        if (err != ESP_OK) {
            memo_upload_set_capture_busy(false);
            ESP_LOGE(TAG, "recorder start failed: %s", esp_err_to_name(err));
            if (bsp_lvgl_lock(200)) {
                arm_ring_hide();
                if (s_list_status != NULL) {
                    lv_label_set_text(s_list_status, "recorder unavailable");
                    lv_obj_set_style_text_color(s_list_status, lv_color_hex(VM_CORAL), 0);
                }
                bsp_lvgl_unlock();
            }
            /* 回到可操作状态，而不是把一个非法迁移塞进状态机。
               录音没开起来，被让出的网络要还回去。 */
            if (s_rec_network_suspended) {
                s_rec_network_suspended = false;
                memo_net_start();
                memo_events_start();
                memo_upload_start();
            }
            s_sm.state = MEMO_STATE_LIST;
            break;
        }
        if (bsp_lvgl_lock(100)) {
            arm_ring_hide();
            if (s_timer_label != NULL) {
                lv_label_set_text(s_timer_label, "00:00");
                lv_obj_set_style_text_color(s_timer_label, lv_color_hex(VM_INK), 0);
            }
            lv_label_set_text(s_record_hint, "release to finish");
            show_screen(s_record_screen);
            bsp_lvgl_unlock();
        }
        s_warned = false;
        strncpy(s_last_id, id, sizeof(s_last_id) - 1);
        if (bsp_lvgl_lock(100)) {
            sheet_visible(false);
            bsp_lvgl_unlock();
        }
        ESP_LOGI(TAG, "recording started: %s", id);
        break;
    }

    case MEMO_ACT_STOP_AND_SAVE: {
        /* 松手（或到点）先落盘，再问用户保存还是取消；此时还没有回列表。 */
        memo_recorder_result_t result;
        esp_err_t err = memo_recorder_stop(true, &result);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "recorded %s (%u ms, %u bytes)",
                     result.id, (unsigned)result.duration_ms, (unsigned)result.size_bytes);
            memo_upload_start();
            memo_events_start();
        } else {
            ESP_LOGE(TAG, "recorder stop: %s; recoverable data retained", esp_err_to_name(err));
            if (err == ESP_ERR_TIMEOUT) {
                s_sm.state = MEMO_STATE_RECORDING; /* Retry joining, never close a live writer. */
                break;
            }
            s_sm.state = MEMO_STATE_LIST;
            memo_upload_set_capture_busy(false);
        }

        if (s_rec_network_suspended) {
            s_rec_network_suspended = false;
            ESP_LOGI(TAG, "restart network: %s", esp_err_to_name(memo_net_start()));
            memo_events_start();
        }
        if (bsp_lvgl_lock(200)) {
            lv_label_set_text(s_record_hint, err == ESP_OK ? "choose save or cancel" : "capture interrupted");
            sheet_visible(err == ESP_OK);
            if (err != ESP_OK) show_screen(s_list_screen);
            refresh_confirm_sheet();
            bsp_lvgl_unlock();
        }
        break;
    }

    case MEMO_ACT_CONFIRM_TOGGLE:
        if (bsp_lvgl_lock(100)) {
            refresh_confirm_sheet();
            bsp_lvgl_unlock();
        }
        break;

    case MEMO_ACT_CONFIRM_SAVE:
        if (memo_spool_move(s_last_id, MEMO_SPOOL_DRAFT, MEMO_SPOOL_PENDING) != ESP_OK) {
            s_sm.state = MEMO_STATE_CONFIRM;
            ESP_LOGE(TAG, "save failed; draft retained for another choice");
            if (bsp_lvgl_lock(100)) {
                lv_label_set_text(s_record_hint, "save failed - retry");
                bsp_lvgl_unlock();
            }
            break;
        }
        memo_upload_set_capture_busy(false);
        ESP_LOGI(TAG, "recording saved: %s", s_last_id);
        if (bsp_lvgl_lock(200)) {
            sheet_visible(false);
            show_screen(s_list_screen);
            refresh_list();
            bsp_lvgl_unlock();
        }
        break;

    case MEMO_ACT_CONFIRM_CANCEL:
        ESP_LOGI(TAG, "recording discarded: %s", s_last_id);
        if (s_last_id[0] != '\0') {
            if (memo_spool_remove(s_last_id, MEMO_SPOOL_DRAFT) != ESP_OK) {
                s_sm.state = MEMO_STATE_CONFIRM;
                ESP_LOGE(TAG, "cancel failed; draft retained for another choice");
                if (bsp_lvgl_lock(100)) {
                    lv_label_set_text(s_record_hint, "cancel failed - retry");
                    bsp_lvgl_unlock();
                }
                break;
            }
            s_last_id[0] = '\0';
        }
        memo_upload_set_capture_busy(false);
        if (bsp_lvgl_lock(200)) {
            sheet_visible(false);
            show_screen(s_list_screen);
            refresh_list();
            bsp_lvgl_unlock();
        }
        break;

    case MEMO_ACT_SELECT_PREV:
        if (s_selected > 0) s_selected--;
        if (bsp_lvgl_lock(100)) {
            refresh_list();
            bsp_lvgl_unlock();
        }
        break;

    case MEMO_ACT_SELECT_NEXT:
        if (s_selected + 1 < s_view_count) s_selected++;
        if (bsp_lvgl_lock(100)) {
            refresh_list();
            bsp_lvgl_unlock();
        }
        break;

    case MEMO_ACT_BACK_TO_LIST:
        memo_player_stop();
        memo_player_drop_cache();
        if (bsp_lvgl_lock(200)) {
            show_screen(s_list_screen);
            refresh_list();
            bsp_lvgl_unlock();
        }
        break;

    case MEMO_ACT_OPEN_DETAIL: {
        if (s_selected >= s_view_count) {
            ESP_LOGI(TAG, "nothing selected");
            s_sm.state = MEMO_STATE_LIST;
            break;
        }
        const list_view_t *entry = &s_view[s_selected];

        /* 服务端条目：拉一次转写全文；拿不到就留空，详情页会给出说明。 */
        char transcript[512];
        transcript[0] = '\0';
        if (!entry->local) {
            if (memo_api_fetch_transcript(entry->id, transcript, sizeof(transcript)) != ESP_OK) {
                transcript[0] = '\0';
            }
        }

        if (bsp_lvgl_lock(200)) {
            if (memo_ui_detail_screen() == NULL) memo_ui_detail_build();
            memo_ui_detail_show(entry->recorded_at, entry->duration_ms,
                                transcript[0] != '\0' ? transcript : NULL);
            /* 没有转写文本时"展开"在屏幕上看不出变化，会让第一次点击像没反应。
               此时直接按"已展开"进入，第一下 OK 就开始播放。 */
            memo_ui_detail_set_expanded(true);
            lv_screen_load(memo_ui_detail_screen());
            bsp_lvgl_unlock();
        }
        s_detail_duration_ms = entry->duration_ms;
        s_sm.state = MEMO_STATE_DETAIL_EXPANDED;

        if (memo_settings_get()->autoplay) {
            play_selected();
            if (memo_player_is_active()) s_sm.state = MEMO_STATE_PLAYING;
        }
        ESP_LOGI(TAG, "detail: %s (%u ms)", entry->id, (unsigned)entry->duration_ms);
        break;
    }

    case MEMO_ACT_EXPAND:
        if (bsp_lvgl_lock(150)) {
            memo_ui_detail_set_expanded(true);
            bsp_lvgl_unlock();
        }
        break;

    case MEMO_ACT_PLAY:
    case MEMO_ACT_RESUME:
        play_selected();
        break;

    case MEMO_ACT_PAUSE:
        /* M3 只支持"停止后从头再播"；真正的暂停/续播随 M4 的流式播放一起做。 */
        memo_player_stop();
        if (bsp_lvgl_lock(150)) {
            memo_ui_detail_set_playback(false, memo_player_position_ms(),
                                        memo_player_duration_ms());
            bsp_lvgl_unlock();
        }
        break;

    case MEMO_ACT_NEXT_MEMO:
    case MEMO_ACT_PREV_MEMO: {
        if (action == MEMO_ACT_NEXT_MEMO && s_selected + 1 < s_view_count) s_selected++;
        if (action == MEMO_ACT_PREV_MEMO && s_selected > 0) s_selected--;
        const list_view_t *row = &s_view[s_selected];
        s_detail_duration_ms = row->duration_ms;

        char transcript[512];
        transcript[0] = '\0';
        if (!row->local) {
            if (memo_api_fetch_transcript(row->id, transcript, sizeof(transcript)) != ESP_OK) {
                transcript[0] = '\0';
            }
        }
        if (bsp_lvgl_lock(200)) {
            memo_ui_detail_show(row->recorded_at, row->duration_ms,
                                transcript[0] != '\0' ? transcript : NULL);
            bsp_lvgl_unlock();
        }
        break;
    }

    case MEMO_ACT_OPEN_SETTINGS:
        if (bsp_lvgl_lock(200)) {
            if (memo_ui_settings_screen() == NULL) memo_ui_settings_build();
            memo_ui_settings_refresh();
            lv_screen_load(memo_ui_settings_screen());
            bsp_lvgl_unlock();
        }
        break;

    case MEMO_ACT_SETTINGS_PREV:
    case MEMO_ACT_SETTINGS_NEXT:
        if (bsp_lvgl_lock(150)) {
            memo_ui_settings_move(action == MEMO_ACT_SETTINGS_NEXT ? 1 : -1);
            bsp_lvgl_unlock();
        }
        break;

    case MEMO_ACT_SUB_PAGE_ENTER:
        if (memo_ui_settings_row() == MEMO_SETTING_NETWORK) {
            /* 网络行不是子页，而是进入蓝牙配网。 */
            esp_err_t events_stop = memo_events_stop();
            esp_err_t upload_stop = memo_upload_stop();
            if (events_stop != ESP_OK || upload_stop != ESP_OK) {
                ESP_LOGW(TAG, "network worker still stopping; provisioning deferred");
                s_network_resume_needed = true;
                s_sm.state = MEMO_STATE_SETTINGS;
                memo_events_start();
                memo_upload_start();
                break;
            }
            memo_discovery_stop();   /* 连同 mDNS 的十几 KB 一起收回 */
            memo_net_suspend();
            if (memo_provision_start() == ESP_OK) {
                s_sm.state = MEMO_STATE_PROVISIONING;
                if (bsp_lvgl_lock(200)) {
                    if (memo_ui_provision_screen() == NULL) memo_ui_provision_build();
                    memo_ui_provision_enter();
                    lv_screen_load(memo_ui_provision_screen());
                    bsp_lvgl_unlock();
                }
            } else {
                ESP_LOGE(TAG, "provisioning start failed");
                s_sm.state = MEMO_STATE_SETTINGS;
            }
            break;
        }
        if (bsp_lvgl_lock(150)) {
            memo_ui_settings_enter();
            const bool in_sheet = memo_ui_settings_in_sheet();
            bsp_lvgl_unlock();
            /* 自动播放是就地切换、只读行什么也不做——两者都不该停在子页状态，
               否则后续上下键会被当成"调值"。 */
            if (!in_sheet) s_sm.state = MEMO_STATE_SETTINGS;
        }
        break;

    case MEMO_ACT_SUB_PAGE_PREV:
    case MEMO_ACT_SUB_PAGE_NEXT:
        if (bsp_lvgl_lock(150)) {
            memo_ui_settings_adjust(action == MEMO_ACT_SUB_PAGE_NEXT ? 1 : -1);
            bsp_lvgl_unlock();
        }
        break;

    case MEMO_ACT_SUB_PAGE_SAVE:
    case MEMO_ACT_SUB_PAGE_CANCEL:
        /* 值在调整时就已经生效并落盘，这里只需要退出子页。 */
        if (bsp_lvgl_lock(150)) {
            memo_ui_settings_refresh();
            bsp_lvgl_unlock();
        }
        break;

    case MEMO_ACT_REJECT_TOGGLE:
        /* The input loop redraws from the state-machine choice. */
        break;

    case MEMO_ACT_REJECT_RETRY:
    case MEMO_ACT_REJECT_DELETE:
        if (!s_reject_deciding &&
            memo_upload_choose(s_reject_id, action == MEMO_ACT_REJECT_DELETE) == ESP_OK) {
            s_reject_deciding = true;
        }
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
        memo_ui_card_visible(s_reject_sheet, s_reject_shadow, false);
        refresh_list();
        bsp_lvgl_unlock();
        s_sm.state = MEMO_STATE_LIST;
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
        lv_obj_t *title = memo_ui_label(s_reject_sheet, memo_font_body(), VM_CORAL);
        lv_label_set_text(title, "上传失败");
        lv_obj_set_pos(title, 12, layout.title_y);
        s_reject_message = memo_ui_label(s_reject_sheet, memo_font_label(), VM_INK);
        lv_obj_set_width(s_reject_message, width - 63);
        lv_label_set_long_mode(s_reject_message, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_line_space(s_reject_message, 0, 0);
        lv_obj_set_pos(s_reject_message, 12, layout.message_y);
        s_reject_retry = memo_ui_label(s_reject_sheet, memo_font_body(), VM_INK);
        lv_label_set_text(s_reject_retry, "重试");
        lv_obj_set_pos(s_reject_retry, 18, layout.options_y);
        s_reject_delete = memo_ui_label(s_reject_sheet, memo_font_body(), VM_MUTED);
        lv_label_set_text(s_reject_delete, "删除");
        lv_obj_set_pos(s_reject_delete, layout.width / 2 + 10, layout.options_y);
        lv_obj_t *hint = memo_ui_label(s_reject_sheet, memo_font_label(), VM_MUTED);
        lv_label_set_text(hint, "上下选择 OK确认");
        lv_obj_set_pos(hint, 12, layout.hint_y);
        memo_font_check_text("upload dialog", "上传失败上传被拒绝请选择重试或删除正在处理处理失败请重新选择上下选择 OK确认待重试");
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
    lv_obj_set_style_text_color(s_reject_retry,
        lv_color_hex(s_sm.reject_delete ? VM_MUTED : VM_INK), 0);
    lv_obj_set_style_text_decor(s_reject_retry,
        s_sm.reject_delete ? LV_TEXT_DECOR_NONE : LV_TEXT_DECOR_UNDERLINE, 0);
    lv_obj_set_style_text_color(s_reject_delete,
        lv_color_hex(s_sm.reject_delete ? VM_CORAL : VM_MUTED), 0);
    lv_obj_set_style_text_decor(s_reject_delete,
        s_sm.reject_delete ? LV_TEXT_DECOR_UNDERLINE : LV_TEXT_DECOR_NONE, 0);
    memo_ui_card_visible(s_reject_sheet, s_reject_shadow, true);
    bsp_lvgl_unlock();
}

static void enqueue_input(memo_input_t input)
{
    if (s_queue == NULL) return;
    if (xQueueSend(s_queue, &input, 0) != pdTRUE) {
        ESP_LOGW(TAG, "input queue full, dropping %s", memo_input_name(input));
    }
}

static void on_key(bsp_btn_t btn, bsp_btn_ev_t ev, void *user)
{
    (void)user;

    const bool was_armed = s_guard.armed;
    if (key_guard_consume(&s_guard, ev == BSP_BTN_RELEASE, esp_timer_get_time())) {
        if (was_armed && !s_guard.armed) {
            ESP_LOGI(TAG, "wake key released, input restored");
        }
        return;
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
    if (input != MEMO_IN_COUNT) enqueue_input(input);
}

static atomic_bool s_ui_ready;

static void poll_draft(void)
{
    if (s_sm.state != MEMO_STATE_LIST || memo_spool_recovery_blocked()) return;
    memo_spool_entry_t entries[MEMO_SPOOL_MAX_ENTRIES];
    const int count = memo_spool_scan(entries, MEMO_SPOOL_MAX_ENTRIES);
    for (int i = 0; i < count && i < MEMO_SPOOL_MAX_ENTRIES; i++) {
        if (entries[i].state != MEMO_SPOOL_DRAFT) continue;
        memo_upload_set_capture_busy(true);
        snprintf(s_last_id, sizeof(s_last_id), "%s", entries[i].id);
        if (bsp_lvgl_lock(200)) {
            s_sm.state = MEMO_STATE_CONFIRM;
            s_sm.confirm_save = true;
            char text[16];
            memo_time_timer(entries[i].duration_ms, text, sizeof(text));
            lv_label_set_text(s_timer_label, text);
            lv_label_set_text(s_record_hint, "recovered audio - choose");
            show_screen(s_record_screen);
            sheet_visible(true);
            refresh_confirm_sheet();
            bsp_lvgl_unlock();
        }
        return;
    }
}

static void input_task(void *arg)
{
    (void)arg;

    int64_t next_tick = 0;
    for (;;) {
        memo_input_t input;
        const bool received = xQueueReceive(s_queue, &input, pdMS_TO_TICKS(50)) == pdTRUE;
        if (!s_ui_ready) continue;
        const int64_t now = esp_timer_get_time();
        if (now >= next_tick) {
            next_tick = now + 200000;
            tick_timer_cb(NULL);
            poll_draft();
            poll_rejected_choice();
        }
        if (!received) continue;
        if (s_sm.state == MEMO_STATE_UPLOAD_REJECTED && s_reject_deciding) continue;

        const memo_state_t before = s_sm.state;
        memo_action_t action = memo_sm_handle(&s_sm, input);
        if (action != MEMO_ACT_NONE) {
            ESP_LOGI(TAG, "%s + %s -> %s (now %s)", memo_state_name(before),
                     memo_input_name(input), memo_action_name(action),
                     memo_state_name(s_sm.state));
            execute(action);
        }
        poll_rejected_choice();
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "voice memo starting");

    memo_sm_init(&s_sm);

    ESP_LOGI(TAG, "heap after boot: %u", (unsigned)esp_get_free_heap_size());

    if (memo_settings_init() != ESP_OK) {
        ESP_LOGW(TAG, "settings unavailable; using defaults");
    }

    const uint32_t wake_causes = esp_sleep_get_wakeup_causes();
    if (wake_causes & (1u << ESP_SLEEP_WAKEUP_GPIO)) {
        key_guard_arm(&s_guard, esp_timer_get_time(), 10LL * 1000 * 1000);
        ESP_LOGI(TAG, "woken by key: swallow input until it is released");
    }

    s_queue = xQueueCreate(8, sizeof(memo_input_t));
    if (s_queue == NULL) {
        ESP_LOGE(TAG, "cannot create input queue");
        return;
    }
    if (xTaskCreate(input_task, "memo_input", 4096, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "cannot create input task");
        return;
    }

    const esp_timer_create_args_t arm_args = {
        .callback = arm_timer_cb, .name = "memo_arm",
    };
    if (esp_timer_create(&arm_args, &s_arm_timer) != ESP_OK) {
        ESP_LOGE(TAG, "cannot create arming timer");
        return;
    }

    esp_err_t i2c_err = bsp_i2c_init();
    if (i2c_err != ESP_OK) {
        ESP_LOGW(TAG, "shared I2C unavailable: %s", esp_err_to_name(i2c_err));
    }

    if (bsp_display_init() != ESP_OK || bsp_lvgl_init() == NULL) {
        ESP_LOGE(TAG, "display/LVGL init failed; check SPI pins");
        return;
    }
    bsp_display_backlight(100);

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

    if (bsp_lvgl_lock(1000)) {
        /* 必须在建任何控件之前挂好 fallback 描述符，否则中文会走 Montserrat 显示成方块。 */
        memo_fonts_init();
        build_list_page();
        build_record_page();
        /* 详情/设置/配网页改为按需构建：三张页面加起来约十几 KB，
           在配网这种内存敏感场景下不值得常驻。 */
        refresh_list();
        show_screen(s_list_screen);
        bsp_lvgl_unlock();
    }

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

    ESP_LOGI(TAG, "heap after audio/battery: %u", (unsigned)esp_get_free_heap_size());
    /* 有凭据就自己连上；没有则等用户从设置里配网。 */
    if (memo_net_start() != ESP_OK) {
        ESP_LOGW(TAG, "network start failed");
    }
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

    if (bsp_button_init(on_key, NULL) != ESP_OK) {
        ESP_LOGE(TAG, "button init failed");
    } else {
        /* 唤醒键可能在按键驱动起来前就松了手，按真实电压补一次同步。 */
        const bool released = bsp_button_read_mv() >= BSP_BTN_MV_RELEASED_MIN;
        key_guard_sync_released(&s_guard, released);
    }

    ESP_LOGI(TAG, "idle; free heap %u (largest block %u)",
             (unsigned)esp_get_free_heap_size(),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    s_ui_ready = true;
}
