// main/memo_app.c —— 语音日记应用外壳（M2：列表空态 + 录音）。
//
// 分工：按键回调只入队；memo_input 任务把事件翻译成状态机输入、执行动作；
// 页面渲染全部在 LVGL 锁内。录音与编码在 recorder 任务里，不占用输入任务。
#include <stdio.h>
#include <string.h>
#include <time.h>

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
#include "memo_net.h"
#include "memo_provision.h"
#include "memo_ui_provision.h"
#include "memo_ui_settings.h"
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
static esp_timer_handle_t s_tick_timer;

/* ---------- 页面对象 ---------- */
static lv_obj_t *s_list_screen;
static lv_obj_t *s_record_screen;
static lv_obj_t *s_list_status;
static lv_obj_t *s_list_hint;
static lv_obj_t *s_timer_label;
static lv_obj_t *s_arm_arc;
static lv_obj_t *s_level_bars[VM_LEVEL_BARS];
static lv_obj_t *s_sheet;
static lv_obj_t *s_sheet_shadow;
static lv_obj_t *s_opt_save;
static lv_obj_t *s_opt_cancel;
static char s_last_id[37];
static uint32_t s_detail_duration_ms; /* 详情页当前条目的时长，播放器未启动时用它 */

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
    lv_anim_set_values(&anim, 0, MEMO_ARMING_MS);
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

static void tick_timer_cb(void *arg)
{
    (void)arg;

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
    if (elapsed >= MEMO_RECORD_LIMIT_MS) {
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
static memo_spool_entry_t s_entries[MEMO_SPOOL_MAX_ENTRIES];
static int s_entry_count;
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

    lv_obj_t *hint = memo_ui_label(s_record_screen, &lv_font_montserrat_14, VM_MUTED);
    lv_label_set_text(hint, "release to finish");
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -VM_INSET - 6);

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

/* 扫描 spool 并重画列表。没有服务端之前，列表里只有本地待上传的录音。 */
static void refresh_list(void)
{
    s_entry_count = memo_spool_scan(s_entries, MEMO_SPOOL_MAX_ENTRIES);
    if (s_entry_count > MEMO_SPOOL_MAX_ENTRIES) s_entry_count = MEMO_SPOOL_MAX_ENTRIES;
    if (s_selected >= s_entry_count) s_selected = s_entry_count > 0 ? s_entry_count - 1 : 0;
    if (s_selected < 0) s_selected = 0;

    const int32_t tz = memo_settings_get()->tz_offset_min;

    for (int i = 0; i < LIST_ROWS; i++) {
        const bool used = i < s_entry_count;
        lv_obj_t *objects[] = {s_rows[i], s_row_shadows[i]};
        for (size_t k = 0; k < 2; k++) {
            if (objects[k] == NULL) continue;
            if (used) {
                lv_obj_clear_flag(objects[k], LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(objects[k], LV_OBJ_FLAG_HIDDEN);
            }
        }
        if (!used) continue;

        char clock[8] = "--:--";
        char duration[16] = "0:00";
        if (s_entries[i].recorded_at > 0) {
            memo_time_clock((int64_t)s_entries[i].recorded_at, tz, clock, sizeof(clock));
            memo_time_duration((int64_t)s_entries[i].duration_ms, duration, sizeof(duration));
        }
        lv_label_set_text(s_row_time[i], clock);
        /* 转写文本要等 M4 与服务端连通，这里先如实写"待上传"。 */
        lv_label_set_text(s_row_text[i], s_entries[i].recorded_at ? "待上传" : "时间未知");
        lv_label_set_text(s_row_dur[i], duration);
        row_highlight(i, i == s_selected);
    }

    if (s_entry_count == 0) {
        lv_label_set_text(s_list_status, "还没有录音");
    } else {
        char buf[32];
        snprintf(buf, sizeof(buf), "%d 条待上传", s_entry_count);
        lv_label_set_text(s_list_status, buf);
    }

    int soc = bsp_battery_soc();
    if (soc < 0) {
        lv_label_set_text(s_battery, "--");
    } else {
        char buf[12];
        snprintf(buf, sizeof(buf), "%d%%", soc);
        lv_label_set_text(s_battery, buf);
    }

    ESP_LOGI(TAG, "list: %d entr%s, free=%u KB", s_entry_count,
             s_entry_count == 1 ? "y" : "ies", (unsigned)(memo_spool_free_bytes() / 1024));
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
        if (bsp_lvgl_lock(100)) {
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
            bsp_lvgl_unlock();
        }
        break;

    case MEMO_ACT_BEGIN_RECORDING: {
        char id[37];
        uint8_t random[16];
        esp_fill_random(random, sizeof(random));
        memo_container_make_uuid(id, random);

        esp_err_t err = memo_recorder_start(id);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "recorder start failed: %s", esp_err_to_name(err));
            if (bsp_lvgl_lock(200)) {
                arm_ring_hide();
                if (s_list_status != NULL) {
                    lv_label_set_text(s_list_status, "recorder unavailable");
                    lv_obj_set_style_text_color(s_list_status, lv_color_hex(VM_CORAL), 0);
                }
                bsp_lvgl_unlock();
            }
            /* 回到可操作状态，而不是把一个非法迁移塞进状态机。 */
            s_sm.state = MEMO_STATE_LIST;
            break;
        }
        if (bsp_lvgl_lock(100)) {
            arm_ring_hide();
            if (s_timer_label != NULL) {
                lv_label_set_text(s_timer_label, "00:00");
                lv_obj_set_style_text_color(s_timer_label, lv_color_hex(VM_INK), 0);
            }
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
        esp_timer_start_periodic(s_tick_timer, 200 * 1000);
        break;
    }

    case MEMO_ACT_STOP_AND_SAVE: {
        /* 松手（或到点）先落盘，再问用户保存还是取消；此时还没有回列表。 */
        esp_timer_stop(s_tick_timer);
        memo_recorder_result_t result;
        esp_err_t err = memo_recorder_stop(true, &result);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "recorded %s (%u ms, %u bytes)",
                     result.id, (unsigned)result.duration_ms, (unsigned)result.size_bytes);
        } else {
            ESP_LOGE(TAG, "recorder stop: %s", esp_err_to_name(err));
        }
        if (bsp_lvgl_lock(200)) {
            sheet_visible(true);
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
        ESP_LOGI(TAG, "recording kept: %s", s_last_id);
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
            memo_spool_remove(s_last_id, MEMO_SPOOL_PENDING);
            s_last_id[0] = '\0';
        }
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
        if (s_selected + 1 < s_entry_count) s_selected++;
        if (bsp_lvgl_lock(100)) {
            refresh_list();
            bsp_lvgl_unlock();
        }
        break;

    case MEMO_ACT_BACK_TO_LIST:
        memo_player_stop();
        esp_timer_stop(s_tick_timer);
        if (bsp_lvgl_lock(200)) {
            show_screen(s_list_screen);
            refresh_list();
            bsp_lvgl_unlock();
        }
        break;

    case MEMO_ACT_OPEN_DETAIL: {
        if (s_selected >= s_entry_count) {
            ESP_LOGI(TAG, "nothing selected");
            s_sm.state = MEMO_STATE_LIST;
            break;
        }
        const memo_spool_entry_t *entry = &s_entries[s_selected];
        if (memo_ui_detail_screen() == NULL) {
            ESP_LOGE(TAG, "detail page was never built");
            break;
        }
        if (bsp_lvgl_lock(200)) {
            memo_ui_detail_show((int64_t)entry->recorded_at, entry->duration_ms, NULL);
            /* 没有转写文本时"展开"在屏幕上看不出变化，会让第一次点击像没反应。
               此时直接按"已展开"进入，第一下 OK 就开始播放。 */
            memo_ui_detail_set_expanded(true);
            lv_screen_load(memo_ui_detail_screen());
            bsp_lvgl_unlock();
        }
        s_detail_duration_ms = entry->duration_ms;
        s_sm.state = MEMO_STATE_DETAIL_EXPANDED;

        if (memo_settings_get()->autoplay) {
            if (memo_player_is_active()) memo_player_stop();
            if (memo_player_play(entry->id) == ESP_OK) {
                s_sm.state = MEMO_STATE_PLAYING;
                ESP_LOGI(TAG, "autoplay %s", entry->id);
            }
        }
        esp_timer_start_periodic(s_tick_timer, 200 * 1000);
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
    case MEMO_ACT_RESUME: {
        if (s_selected >= s_entry_count) break;
        const char *id = s_entries[s_selected].id;
        if (memo_player_is_active()) memo_player_stop();
        if (memo_player_play(id) == ESP_OK) {
            ESP_LOGI(TAG, "playing %s", id);
        } else {
            ESP_LOGE(TAG, "play failed");
        }
        break;
    }

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
        if (action == MEMO_ACT_NEXT_MEMO && s_selected + 1 < s_entry_count) s_selected++;
        if (action == MEMO_ACT_PREV_MEMO && s_selected > 0) s_selected--;
        const memo_spool_entry_t *entry = &s_entries[s_selected];
        s_detail_duration_ms = entry->duration_ms;
        if (bsp_lvgl_lock(200)) {
            memo_ui_detail_show((int64_t)entry->recorded_at, entry->duration_ms, NULL);
            bsp_lvgl_unlock();
        }
        break;
    }

    case MEMO_ACT_OPEN_SETTINGS:
        if (memo_ui_settings_screen() == NULL) {
            ESP_LOGE(TAG, "settings page was never built");
            break;
        }
        if (bsp_lvgl_lock(200)) {
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
            memo_net_suspend(); /* 配网要独占 WiFi */
            if (memo_provision_start() == ESP_OK) {
                s_sm.state = MEMO_STATE_PROVISIONING;
                if (bsp_lvgl_lock(200)) {
                    memo_ui_provision_enter();
                    lv_screen_load(memo_ui_provision_screen());
                    bsp_lvgl_unlock();
                }
                esp_timer_start_periodic(s_tick_timer, 200 * 1000);
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

    default:
        break;
    }
}

/* ---------- 输入任务 ---------- */
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

static void input_task(void *arg)
{
    (void)arg;

    for (;;) {
        memo_input_t input;
        if (xQueueReceive(s_queue, &input, portMAX_DELAY) != pdTRUE) continue;

        const memo_state_t before = s_sm.state;
        memo_action_t action = memo_sm_handle(&s_sm, input);
        if (action != MEMO_ACT_NONE) {
            ESP_LOGI(TAG, "%s + %s -> %s (now %s)", memo_state_name(before),
                     memo_input_name(input), memo_action_name(action),
                     memo_state_name(s_sm.state));
            execute(action);
        }
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "voice memo starting");

    memo_sm_init(&s_sm);

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
    const esp_timer_create_args_t tick_args = {
        .callback = tick_timer_cb, .name = "memo_tick",
    };
    if (esp_timer_create(&arm_args, &s_arm_timer) != ESP_OK ||
        esp_timer_create(&tick_args, &s_tick_timer) != ESP_OK) {
        ESP_LOGE(TAG, "cannot create timers");
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
    if (memo_spool_mount() != ESP_OK) {
        ESP_LOGE(TAG, "spool mount failed; recordings will not persist");
    } else {
        const int dropped = memo_spool_drop_empty();
        if (dropped > 0) {
            ESP_LOGW(TAG, "removed %d incomplete recording(s)", dropped);
        }
    }

    if (bsp_lvgl_lock(1000)) {
        /* 必须在建任何控件之前挂好 fallback 描述符，否则中文会走 Montserrat 显示成方块。 */
        memo_fonts_init();
        build_list_page();
        build_record_page();
        memo_ui_detail_build();
        memo_ui_settings_build();
        memo_ui_provision_build();
        refresh_list();
        show_screen(s_list_screen);
        bsp_lvgl_unlock();
    }

    /* 录音通路依赖 codec：不先 init，bsp_audio_set_format 会直接返回
       ESP_ERR_INVALID_STATE，表现就是"按了没反应"。 */
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

    /* 有凭据就自己连上；没有则等用户从设置里配网。 */
    if (memo_net_start() != ESP_OK) {
        ESP_LOGW(TAG, "network start failed");
    }

    if (bsp_button_init(on_key, NULL) != ESP_OK) {
        ESP_LOGE(TAG, "button init failed");
    } else {
        /* 唤醒键可能在按键驱动起来前就松了手，按真实电压补一次同步。 */
        const bool released = bsp_button_read_mv() >= BSP_BTN_MV_RELEASED_MIN;
        key_guard_sync_released(&s_guard, released);
    }

    ESP_LOGI(TAG, "idle");
}
