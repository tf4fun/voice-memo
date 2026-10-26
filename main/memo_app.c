// main/memo_app.c —— 语音日记应用入口。
//
// M0 骨架：初始化 BSP 与 LVGL，画出第一张卡片（视觉设计语言里的 card 组件），
// 用来确认新的分区表、sdkconfig 调参与显示链路都还正常。页面内容在 M3 落地。
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "lvgl.h"

#include "esp_log.h"

static const char *TAG = "memo_app";

// 视觉设计语言里的取值，后续抽到 memo_ui_common。
#define VM_PAPER 0xF6F1E6
#define VM_CARD  0xFFFFFF
#define VM_INK   0x141210
#define VM_MUTED 0x7A7368

#define VM_RADIUS_CARD 12
#define VM_STROKE      2
#define VM_SHADOW      3
#define VM_INSET       8

// 去掉默认样式，只留一个纯色圆角矩形：卡片体系的唯一基础件。
static void flat_rect(lv_obj_t *obj, uint32_t color, int radius)
{
    lv_obj_remove_style_all(obj);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

// 卡片 = 实心墨块 + 白底墨线圆角矩形。硬投影用第二张矩形画，不用 LVGL 的模糊阴影。
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

static void build_ui(void)
{
    lv_obj_t *screen = lv_screen_active();
    lv_obj_remove_style_all(screen);
    lv_obj_set_style_bg_color(screen, lv_color_hex(VM_PAPER), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    const int width = lv_display_get_horizontal_resolution(NULL);
    const int card_w = width - 2 * VM_INSET - VM_SHADOW;

    lv_obj_t *panel = card_create(screen, VM_INSET, 96, card_w, 104);

    lv_obj_t *title = lv_label_create(panel);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(VM_INK), 0);
    lv_label_set_text(title, "MEMO");
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 14, 14);

    lv_obj_t *note = lv_label_create(panel);
    lv_obj_set_style_text_font(note, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(note, lv_color_hex(VM_MUTED), 0);
    lv_label_set_text(note, "skeleton");
    lv_obj_align(note, LV_ALIGN_TOP_LEFT, 14, 44);
}

void app_main(void)
{
    ESP_LOGI(TAG, "voice memo starting");

    // 共享 I2C 是可选外设的入口；失败只降级，不阻塞显示。
    esp_err_t i2c_err = bsp_i2c_init();
    if (i2c_err != ESP_OK) {
        ESP_LOGW(TAG, "shared I2C unavailable: %s", esp_err_to_name(i2c_err));
    } else {
        bsp_i2c_scan();
    }

    if (bsp_display_init() != ESP_OK || bsp_lvgl_init() == NULL) {
        ESP_LOGE(TAG, "display/LVGL init failed; check SPI pins");
        return;
    }
    bsp_display_backlight(100);

    if (!bsp_lvgl_lock(1000)) {
        ESP_LOGE(TAG, "LVGL lock timeout during startup");
        return;
    }
    build_ui();
    bsp_lvgl_unlock();

    ESP_LOGI(TAG, "idle");
}
