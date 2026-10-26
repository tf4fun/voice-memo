// main/memo_app.c —— 语音日记应用外壳（M2：列表空态 + 录音）。
//
// 分工：按键回调只入队；memo_input 任务把事件翻译成状态机输入、执行动作；
// 页面渲染全部在 LVGL 锁内。录音与编码在 recorder 任务里，不占用输入任务。
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_random.h"
#include "esp_sleep.h"
#include "esp_timer.h"

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

/* ---------- 小工具 ---------- */
static void flat_rect(lv_obj_t *obj, uint32_t color, int radius)
{
    lv_obj_remove_style_all(obj);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

static lv_obj_t *card_create(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *shadow = lv_obj_create(parent);
    flat_rect(shadow, VM_INK, VM_RADIUS_CARD);
    lv_obj_set_pos(shadow, x + VM_SHADOW, y + VM_SHADOW);
    lv_obj_set_size(shadow, w, h);

    lv_obj_t *face = lv_obj_create(parent);
    flat_rect(face, VM_CARD, VM_RADIUS_CARD);
    lv_obj_set_style_border_color(face, lv_color_hex(VM_INK), 0);
    lv_obj_set_style_border_width(face, VM_STROKE, 0);
    lv_obj_set_pos(face, x, y);
    lv_obj_set_size(face, w, h);
    return face;
}

static lv_obj_t *label_create(lv_obj_t *parent, const lv_font_t *font, uint32_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    return label;
}

static void enqueue_input(memo_input_t input);

static void arm_timer_cb(void *arg)
{
    (void)arg;
    enqueue_input(MEMO_IN_ARM_COMPLETE);
}

static void tick_timer_cb(void *arg)
{
    (void)arg;
    if (memo_recorder_elapsed_ms() >= MEMO_RECORD_LIMIT_MS) {
        enqueue_input(MEMO_IN_RECORD_LIMIT);
        return;
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
static void build_list_page(void)
{
    s_list_screen = lv_obj_create(NULL);
    flat_rect(s_list_screen, VM_PAPER, 0);
    lv_obj_set_style_radius(s_list_screen, 0, 0);

    lv_obj_t *title = label_create(s_list_screen, &lv_font_montserrat_20, VM_INK);
    lv_label_set_text(title, "MEMO");
    lv_obj_set_pos(title, VM_INSET + 4, VM_INSET + 6);

    const int width = lv_display_get_horizontal_resolution(NULL);
    lv_obj_t *panel = card_create(s_list_screen, VM_INSET, 96,
                                  width - 2 * VM_INSET - VM_SHADOW, 104);

    s_list_status = label_create(panel, &lv_font_montserrat_14, VM_INK);
    lv_label_set_text(s_list_status, "-");
    lv_obj_align(s_list_status, LV_ALIGN_TOP_LEFT, 14, 14);

    lv_obj_t *sub = label_create(panel, &lv_font_montserrat_14, VM_MUTED);
    lv_label_set_text(sub, "hold OK to record");
    lv_obj_align(sub, LV_ALIGN_TOP_LEFT, 14, 44);

    s_list_hint = label_create(s_list_screen, &lv_font_montserrat_14, VM_MUTED);
    lv_label_set_text(s_list_hint, "M2 skeleton");
    lv_obj_align(s_list_hint, LV_ALIGN_BOTTOM_LEFT, VM_INSET + 4, -VM_INSET);

    /* 待录音圆环：平时隐藏，ARMING 时显示并逐步填满。 */
    s_arm_arc = lv_arc_create(s_list_screen);
    lv_obj_set_size(s_arm_arc, 76, 76);
    lv_obj_center(s_arm_arc);
    lv_arc_set_rotation(s_arm_arc, 270);
    lv_arc_set_bg_angles(s_arm_arc, 0, 360);
    lv_arc_set_range(s_arm_arc, 0, MEMO_ARMING_MS);
    lv_arc_set_value(s_arm_arc, 0);
    lv_obj_remove_style(s_arm_arc, NULL, LV_PART_KNOB);
    lv_obj_set_style_arc_color(s_arm_arc, lv_color_hex(VM_PAPER), LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_arm_arc, 8, LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_arm_arc, lv_color_hex(VM_CORAL), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(s_arm_arc, 8, LV_PART_INDICATOR);
    lv_obj_add_flag(s_arm_arc, LV_OBJ_FLAG_HIDDEN);
}

static void build_record_page(void)
{
    s_record_screen = lv_obj_create(NULL);
    flat_rect(s_record_screen, VM_PAPER, 0);
    lv_obj_set_style_radius(s_record_screen, 0, 0);

    lv_obj_t *dot = lv_obj_create(s_record_screen);
    flat_rect(dot, VM_CORAL, LV_RADIUS_CIRCLE);
    lv_obj_set_size(dot, 12, 12);
    lv_obj_set_pos(dot, VM_INSET + 6, VM_INSET + 10);

    lv_obj_t *rec = label_create(s_record_screen, &lv_font_montserrat_14, VM_INK);
    lv_label_set_text(rec, "REC");
    lv_obj_set_pos(rec, VM_INSET + 26, VM_INSET + 6);

    s_timer_label = label_create(s_record_screen, &lv_font_montserrat_40, VM_INK);
    lv_label_set_text(s_timer_label, "00:00");
    lv_obj_align(s_timer_label, LV_ALIGN_TOP_MID, 0, 70);

    /* 十二根离散电平条，读起来像贴纸，也便宜。 */
    const int bar_w = 12, gap = 4;
    const int total = VM_LEVEL_BARS * bar_w + (VM_LEVEL_BARS - 1) * gap;
    int x = (lv_display_get_horizontal_resolution(NULL) - total) / 2;
    for (int i = 0; i < VM_LEVEL_BARS; i++) {
        lv_obj_t *bar = lv_obj_create(s_record_screen);
        flat_rect(bar, VM_PAPER, 3);
        lv_obj_set_style_border_color(bar, lv_color_hex(VM_INK), 0);
        lv_obj_set_style_border_width(bar, 1, 0);
        lv_obj_set_size(bar, bar_w, 20);
        lv_obj_set_pos(bar, x + i * (bar_w + gap), 190);
        s_level_bars[i] = bar;
    }

    lv_obj_t *hint = label_create(s_record_screen, &lv_font_montserrat_14, VM_MUTED);
    lv_label_set_text(hint, "OK stop   hold OK cancel");
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -VM_INSET - 6);
}

static void refresh_list_labels(void)
{
    memo_spool_entry_t entries[MEMO_SPOOL_MAX_ENTRIES];
    int total = memo_spool_scan(entries, MEMO_SPOOL_MAX_ENTRIES);
    int pending = 0;
    for (int i = 0; i < total && i < MEMO_SPOOL_MAX_ENTRIES; i++) {
        if (entries[i].state == MEMO_SPOOL_PENDING) pending++;
    }

    char text[48];
    snprintf(text, sizeof(text), "pending %d / total %d", pending, total);
    if (s_list_status != NULL) lv_label_set_text(s_list_status, text);

    snprintf(text, sizeof(text), "free %u KB", (unsigned)(memo_spool_free_bytes() / 1024));
    if (s_list_hint != NULL) lv_label_set_text(s_list_hint, text);
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
            if (s_arm_arc != NULL) {
                lv_obj_clear_flag(s_arm_arc, LV_OBJ_FLAG_HIDDEN);
                lv_arc_set_value(s_arm_arc, 0);
            }
            bsp_lvgl_unlock();
        }
        esp_timer_stop(s_arm_timer);
        esp_timer_start_once(s_arm_timer, (uint64_t)MEMO_ARMING_MS * 1000);
        break;

    case MEMO_ACT_CANCEL_ARMING:
        esp_timer_stop(s_arm_timer);
        if (bsp_lvgl_lock(100)) {
            if (s_arm_arc != NULL) lv_obj_add_flag(s_arm_arc, LV_OBJ_FLAG_HIDDEN);
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
            s_sm.state = MEMO_STATE_ERROR;
            if (bsp_lvgl_lock(100)) {
                if (s_arm_arc != NULL) lv_obj_add_flag(s_arm_arc, LV_OBJ_FLAG_HIDDEN);
                bsp_lvgl_unlock();
            }
            break;
        }
        if (bsp_lvgl_lock(100)) {
            if (s_arm_arc != NULL) lv_obj_add_flag(s_arm_arc, LV_OBJ_FLAG_HIDDEN);
            if (s_timer_label != NULL) {
                lv_label_set_text(s_timer_label, "00:00");
                lv_obj_set_style_text_color(s_timer_label, lv_color_hex(VM_INK), 0);
            }
            show_screen(s_record_screen);
            bsp_lvgl_unlock();
        }
        esp_timer_start_periodic(s_tick_timer, 200 * 1000);
        break;
    }

    case MEMO_ACT_STOP_AND_SAVE:
    case MEMO_ACT_CANCEL_RECORDING: {
        esp_timer_stop(s_tick_timer);
        bool keep = (action == MEMO_ACT_STOP_AND_SAVE);
        memo_recorder_result_t result;
        esp_err_t err = memo_recorder_stop(keep, &result);
        if (err == ESP_OK && keep) {
            ESP_LOGI(TAG, "saved %s (%u ms, %u bytes)", result.id,
                     (unsigned)result.duration_ms, (unsigned)result.size_bytes);
        } else if (err != ESP_OK) {
            ESP_LOGE(TAG, "recorder stop: %s", esp_err_to_name(err));
        }
        if (bsp_lvgl_lock(200)) {
            show_screen(s_list_screen);
            refresh_list_labels();
            bsp_lvgl_unlock();
        }
        break;
    }

    case MEMO_ACT_BACK_TO_LIST:
        if (bsp_lvgl_lock(200)) {
            show_screen(s_list_screen);
            refresh_list_labels();
            bsp_lvgl_unlock();
        }
        break;

    case MEMO_ACT_OPEN_DETAIL:
    case MEMO_ACT_OPEN_SETTINGS:
        ESP_LOGI(TAG, "action %s arrives in M3", memo_action_name(action));
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

        memo_action_t action = memo_sm_handle(&s_sm, input);
        if (action != MEMO_ACT_NONE) {
            ESP_LOGD(TAG, "state %s + %s -> %s", memo_state_name(s_sm.state),
                     memo_input_name(input), memo_action_name(action));
            execute(action);
        }
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "voice memo starting");

    memo_sm_init(&s_sm);

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

    if (bsp_lvgl_lock(1000)) {
        build_list_page();
        build_record_page();
        refresh_list_labels();
        show_screen(s_list_screen);
        bsp_lvgl_unlock();
    }

    if (memo_recorder_init() != ESP_OK) {
        ESP_LOGE(TAG, "recorder unavailable; recording will not work");
    }
    bsp_battery_init();

    if (bsp_button_init(on_key, NULL) != ESP_OK) {
        ESP_LOGE(TAG, "button init failed");
    } else {
        /* 唤醒键可能在按键驱动起来前就松了手，按真实电压补一次同步。 */
        const bool released = bsp_button_read_mv() >= BSP_BTN_MV_RELEASED_MIN;
        key_guard_sync_released(&s_guard, released);
    }

    ESP_LOGI(TAG, "idle");
}
