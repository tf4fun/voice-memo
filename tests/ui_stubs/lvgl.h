#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
typedef struct lv_font { int line_height; const struct lv_font *fallback; } lv_font_t;
typedef struct { bool is_placeholder; } lv_font_glyph_dsc_t;
typedef int lv_label_long_mode_t;
typedef struct lv_obj {
    int x,y,w,h,color,pad,line_space,long_mode,scroll_y,dir,alignment;
    bool hidden,label,alive;
    char text[4096];
    const lv_font_t *font;
    struct lv_obj *parent,*children[64];
    int child_count, text_updates, long_mode_updates, marquee_offset;
    int animations_started, anim_from, anim_to, anim_duration, rotation;
    uint32_t anim_repeat;
    bool animating;
    bool height_fixed;
} lv_obj_t;
#define LV_FONT_DECLARE(name) extern const lv_font_t name
LV_FONT_DECLARE(lv_font_montserrat_14);
LV_FONT_DECLARE(lv_font_montserrat_20);
LV_FONT_DECLARE(lv_font_unscii_16);
#define LV_OBJ_FLAG_SCROLLABLE 1
#define LV_OBJ_FLAG_HIDDEN 2
#define LV_OPA_COVER 255
#define LV_ALIGN_TOP_RIGHT 1
#define LV_ALIGN_LEFT_MID 2
#define LV_ALIGN_RIGHT_MID 3
#define LV_ALIGN_TOP_MID 4
#define LV_ALIGN_CENTER 5
#define LV_ALIGN_BOTTOM_LEFT 6
#define LV_ALIGN_TOP_LEFT 7
#define LV_TEXT_ALIGN_CENTER 1
#define LV_TEXT_ALIGN_RIGHT 2
void lv_obj_set_style_text_align(lv_obj_t *o,int value,int selector);
#define LV_LABEL_LONG_WRAP 1
#define LV_LABEL_LONG_CLIP 2
#define LV_LABEL_LONG_DOT 3
#define LV_LABEL_LONG_SCROLL_CIRCULAR 4
#define LV_DIR_VER 1
#define LV_DIR_NONE 0
#define LV_ANIM_OFF 0
#define LV_SYMBOL_LEFT "<"
#define LV_SYMBOL_PLAY ">"
#define LV_SYMBOL_PAUSE "||"
#define LV_SYMBOL_WIFI "W"
#define LV_SYMBOL_DRIVE "D"
#define LV_SYMBOL_VOLUME_MAX "V"
#define LV_SYMBOL_EYE_OPEN "E"
#define LV_SYMBOL_POWER "P"
#define LV_SYMBOL_SETTINGS "*"
int lv_display_get_horizontal_resolution(void *display);
int lv_display_get_vertical_resolution(void *display);
lv_obj_t *lv_obj_create(lv_obj_t *parent);
void lv_obj_delete(lv_obj_t *obj);
lv_obj_t *lv_obj_get_parent(const lv_obj_t *obj);
int ui_test_live_objects(void);
lv_obj_t *lv_label_create(lv_obj_t *parent);
void lv_obj_remove_style_all(lv_obj_t *obj);
int lv_color_hex(uint32_t color);
void lv_obj_set_style_bg_color(lv_obj_t *o,int value,int selector);
void lv_obj_set_style_bg_opa(lv_obj_t *o,int value,int selector);
void lv_obj_set_style_radius(lv_obj_t *o,int value,int selector);
void lv_obj_set_style_border_color(lv_obj_t *o,int value,int selector);
void lv_obj_set_style_border_width(lv_obj_t *o,int value,int selector);
void lv_obj_set_style_translate_y(lv_obj_t *o,int value,int selector);
void lv_obj_set_style_pad_all(lv_obj_t *o,int value,int selector);
void lv_obj_set_style_pad_hor(lv_obj_t *o,int value,int selector);
void lv_obj_set_style_text_color(lv_obj_t *o,int value,int selector);
void lv_obj_set_style_text_line_space(lv_obj_t *o,int value,int selector);
void lv_obj_set_style_text_font(lv_obj_t *o,const lv_font_t *font,int selector);
void lv_obj_set_hidden(lv_obj_t *o,bool en);
void lv_obj_set_scrollable(lv_obj_t *o,bool en);
void lv_obj_set_clickable(lv_obj_t *o,bool en);
lv_obj_t *lv_obj_get_child(lv_obj_t *o,int idx);
void lv_obj_set_pos(lv_obj_t *o,int x,int y);
void lv_obj_set_size(lv_obj_t *o,int w,int h);
void lv_obj_set_width(lv_obj_t *o,int w);
int lv_obj_get_width(lv_obj_t *o);
void lv_obj_set_height(lv_obj_t *o,int h);
void lv_obj_align(lv_obj_t *o,int align,int x,int y);
void lv_obj_set_scroll_dir(lv_obj_t *o,int dir);
void lv_label_set_text(lv_obj_t *o,const char *text);
void lv_label_set_text_static(lv_obj_t *o,const char *text);
const char *lv_label_get_text(lv_obj_t *o);
void lv_label_set_long_mode(lv_obj_t *o,int mode);
lv_label_long_mode_t lv_label_get_long_mode(lv_obj_t *o);
void lv_obj_update_layout(lv_obj_t *o);
int lv_obj_get_scroll_y(lv_obj_t *o);
int lv_obj_get_scroll_bottom(lv_obj_t *o);
void lv_obj_scroll_to_y(lv_obj_t *o,int y,int animation);
bool lv_font_get_glyph_dsc(const lv_font_t *font,lv_font_glyph_dsc_t *glyph,uint32_t cp,uint32_t next);

LV_FONT_DECLARE(lv_font_montserrat_40);
#define LV_OPA_TRANSP 0
#define LV_OPA_20 51
#define LV_RADIUS_CIRCLE 1000
#define LV_PART_MAIN 0
#define LV_PART_INDICATOR 1
#define LV_PART_KNOB 2
#define LV_TEXT_DECOR_NONE 0
#define LV_TEXT_DECOR_UNDERLINE 1
#define LV_OBJ_FLAG_CLICKABLE 4
#define LV_ALIGN_BOTTOM_MID 8
typedef struct { void *var; void (*exec)(void *,int32_t); int first,last; uint32_t duration,repeat; } lv_anim_t;
#define LV_ANIM_REPEAT_INFINITE UINT32_MAX
void lv_anim_init(lv_anim_t *anim);
void lv_anim_set_var(lv_anim_t *anim,void *var);
void lv_anim_set_values(lv_anim_t *anim,int first,int last);
void lv_anim_set_duration(lv_anim_t *anim,uint32_t duration);
void lv_anim_set_repeat_count(lv_anim_t *anim,uint32_t count);
void lv_anim_set_exec_cb(lv_anim_t *anim,void (*exec)(void *,int32_t));
void lv_anim_start(lv_anim_t *anim);
void lv_anim_delete(void *var,void (*exec)(void *,int32_t));
lv_obj_t *lv_arc_create(lv_obj_t *parent);
void lv_arc_set_value(lv_obj_t *obj,int value);
void lv_arc_set_rotation(lv_obj_t *obj,int rotation);
void lv_arc_set_range(lv_obj_t *obj,int min,int max);
void lv_arc_set_bg_angles(lv_obj_t *obj,int first,int last);
void lv_obj_set_style_arc_color(lv_obj_t *obj,int value,int selector);
void lv_obj_set_style_arc_opa(lv_obj_t *obj,int value,int selector);
void lv_obj_set_style_arc_width(lv_obj_t *obj,int value,int selector);
void lv_obj_set_style_text_decor(lv_obj_t *obj,int value,int selector);
void lv_screen_load(lv_obj_t *screen);
lv_obj_t *lv_screen_active(void);
extern void (*ui_test_access)(void);
