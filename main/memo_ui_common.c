// main/memo_ui_common.c
#include "memo_ui_common.h"

void memo_ui_flat_rect(lv_obj_t *obj, uint32_t color, int radius)
{
    lv_obj_remove_style_all(obj);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

lv_obj_t *memo_ui_card(lv_obj_t *parent, int x, int y, int w, int h, lv_obj_t **shadow_out)
{
    lv_obj_t *shadow = lv_obj_create(parent);
    memo_ui_flat_rect(shadow, VM_INK, VM_RADIUS_CARD);
    lv_obj_set_pos(shadow, x + VM_SHADOW, y + VM_SHADOW);
    lv_obj_set_size(shadow, w, h);
    if (shadow_out != NULL) *shadow_out = shadow;

    lv_obj_t *face = lv_obj_create(parent);
    memo_ui_flat_rect(face, VM_CARD, VM_RADIUS_CARD);
    lv_obj_set_style_border_color(face, lv_color_hex(VM_INK), 0);
    lv_obj_set_style_border_width(face, VM_STROKE, 0);
    lv_obj_set_pos(face, x, y);
    lv_obj_set_size(face, w, h);
    return face;
}

lv_obj_t *memo_ui_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    return label;
}

void memo_ui_card_visible(lv_obj_t *card, lv_obj_t *shadow, bool visible)
{
    lv_obj_t *objects[] = {card, shadow};
    for (size_t i = 0; i < sizeof(objects) / sizeof(objects[0]); i++) {
        if (objects[i] == NULL) continue;
        if (visible) {
            lv_obj_clear_flag(objects[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(objects[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}
