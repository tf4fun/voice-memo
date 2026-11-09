// main/memo_ui_common.c
#include "memo_ui_common.h"
#include "memo_browse.h"
#include "memo_font.h"
#include "memo_progress.h"
#include <stdio.h>

void memo_ui_flat_rect(lv_obj_t *obj, uint32_t color, int radius)
{
    lv_obj_remove_style_all(obj);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_set_scrollable(obj, false);
}

lv_obj_t *memo_ui_card(lv_obj_t *parent, int x, int y, int w, int h, lv_obj_t **shadow_out)
{
    if (shadow_out != NULL) *shadow_out = NULL;

    lv_obj_t *face = lv_obj_create(parent);
    memo_ui_flat_rect(face, TERM_BG, VM_RADIUS_CARD);
    lv_obj_set_style_border_color(face, lv_color_hex(TERM_DARK), 0);
    lv_obj_set_style_border_width(face, VM_STROKE, 0);
    lv_obj_set_pos(face, x, y);
    lv_obj_set_size(face, w, h);
    return face;
}

void memo_ui_rule(lv_obj_t *screen, int x, int y, int w)
{
    lv_obj_t *rule = lv_obj_create(screen);
    memo_ui_flat_rect(rule, TERM_DARK, 0);
    lv_obj_set_size(rule, w, 1);
    lv_obj_set_pos(rule, x, y);
}

lv_obj_t *memo_ui_meter_create(lv_obj_t *parent, int width, int height, lv_obj_t **fill)
{
    lv_obj_t *track = lv_obj_create(parent);
    memo_ui_flat_rect(track, TERM_BG, 0);
    lv_obj_set_style_pad_all(track, 0, 0);
    lv_obj_set_style_border_width(track, 1, 0);
    lv_obj_set_style_border_color(track, lv_color_hex(TERM_FG), 0);
    lv_obj_set_size(track, width, height);
    const memo_progress_layout_t layout = memo_progress_layout(width, height);
    *fill = lv_obj_create(track);
    memo_ui_flat_rect(*fill, TERM_FG, 0);
    lv_obj_set_pos(*fill, layout.x, layout.y);
    lv_obj_set_size(*fill, 0, layout.height);
    return track;
}

void memo_ui_inverse(lv_obj_t *box, bool selected)
{
    if (box == NULL) return;
    lv_obj_set_style_bg_color(box, lv_color_hex(selected ? TERM_FG : TERM_BG), 0);
    lv_obj_set_style_border_color(box, lv_color_hex(selected ? TERM_FG : TERM_DARK), 0);
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
        lv_obj_set_hidden(objects[i], !visible);
    }
}

lv_obj_t *memo_ui_scrollbar_create(lv_obj_t *screen, int x, int y, int h)
{
    lv_obj_t *track = lv_obj_create(screen);
    memo_ui_flat_rect(track, TERM_BG, 0);
    lv_obj_set_style_border_color(track, lv_color_hex(TERM_DARK), 0);
    lv_obj_set_style_border_width(track, 1, 0);
    lv_obj_set_pos(track, x, y);
    lv_obj_set_size(track, 4, h);

    lv_obj_t *thumb = lv_obj_create(track);
    memo_ui_flat_rect(thumb, TERM_FG, 0);
    lv_obj_set_pos(thumb, 0, 0);
    lv_obj_set_size(thumb, 2, 8);
    lv_obj_set_hidden(track, true);
    return track;
}

void memo_ui_scrollbar_update(lv_obj_t *track, int track_h, int total, int visible, int top)
{
    if (track == NULL) return;
    const memo_scrollbar_t thumb = memo_scrollbar_thumb(total, visible, top, track_h - 2, 8);
    lv_obj_set_hidden(track, !thumb.visible);
    if (!thumb.visible) return;
    lv_obj_t *child = lv_obj_get_child(track, 0);
    if (child == NULL) return;
    lv_obj_set_pos(child, 0, thumb.offset);
    lv_obj_set_size(child, 2, thumb.length);
}

static lv_obj_t *s_batteries[6];
static int s_battery_count;
static int s_soc = -1;

void memo_ui_screen_delete(lv_obj_t *screen)
{
    if (screen == NULL) return;
    for (int i = 0; i < s_battery_count;) {
        if (lv_obj_get_parent(s_batteries[i]) == screen) {
            s_batteries[i] = s_batteries[--s_battery_count];
            s_batteries[s_battery_count] = NULL;
        } else i++;
    }
    lv_obj_delete(screen);
}

void memo_ui_battery_update(int soc)
{
    s_soc = soc;
    char text[12] = "--";
    if (soc >= 0) snprintf(text, sizeof(text), "%d%%", soc > 100 ? 100 : soc);
    for (int i = 0; i < s_battery_count; i++) lv_label_set_text(s_batteries[i], text);
}

lv_obj_t *memo_ui_battery(lv_obj_t *screen)
{
    lv_obj_t *label = memo_ui_label(screen, memo_font_mono(), TERM_DIM);
    lv_obj_set_pos(label, lv_display_get_horizontal_resolution(NULL) - 56, 16);
    lv_obj_set_size(label, 40, memo_font_mono()->line_height);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_RIGHT, 0);
    if (s_battery_count < (int)(sizeof(s_batteries)/sizeof(s_batteries[0])))
        s_batteries[s_battery_count++] = label;
    memo_ui_battery_update(s_soc);
    return label;
}

lv_obj_t *memo_ui_header(lv_obj_t *screen, const char *text)
{
    lv_obj_t *title = memo_ui_label(screen, memo_font_label(), TERM_FG);
    lv_label_set_text(title, text);
    lv_obj_set_pos(title, 24, 8);
    lv_obj_set_size(title, 144, memo_font_label()->line_height);
    lv_label_set_long_mode(title, LV_LABEL_LONG_CLIP);
    memo_ui_battery(screen);
    memo_ui_rule(screen, 16, 44, lv_display_get_horizontal_resolution(NULL) - 32);
    return title;
}
