// Object/scroll model for C UI bindings. This is not a pixel renderer.
#include "lvgl.h"
#include <assert.h>
#include <string.h>
static lv_obj_t objects[256];
void (*ui_test_access)(void);
#define ACCESS() do { if(ui_test_access) ui_test_access(); } while(0)
static lv_obj_t *active;
static int count;
const lv_font_t memo_font_16={.line_height=31},memo_font_20={.line_height=38};
const lv_font_t lv_font_montserrat_14={.line_height=17},lv_font_montserrat_20={.line_height=24};
const lv_font_t lv_font_montserrat_40={.line_height=48};
const lv_font_t lv_font_unscii_16={.line_height=16};
const lv_font_t memo_font_terminal={.line_height=18};
int lv_display_get_horizontal_resolution(void *d){(void)d;return 240;}
int lv_display_get_vertical_resolution(void *d){(void)d;return 320;}
lv_obj_t *lv_obj_create(lv_obj_t *p){ACCESS();assert(count<256);lv_obj_t *o=NULL;
 for(int i=0;i<256;i++)if(!objects[i].alive){o=&objects[i];break;}
 assert(o);memset(o,0,sizeof(*o));o->alive=true;count++;o->parent=p;o->w=240;o->h=320;
 if(p){assert(p->alive && p->child_count<64);p->children[p->child_count++]=o;}return o;}
int ui_test_live_objects(void){return count;}
lv_obj_t *lv_obj_get_parent(const lv_obj_t *o){ACCESS();assert(o && o->alive);return o->parent;}
void lv_obj_delete(lv_obj_t *o){ACCESS();assert(o && o->alive && o!=active);
 while(o->child_count)lv_obj_delete(o->children[o->child_count-1]);
 if(o->parent){lv_obj_t *p=o->parent;int at=0;while(at<p->child_count && p->children[at]!=o)at++;
  assert(at<p->child_count);memmove(p->children+at,p->children+at+1,(p->child_count-at-1)*sizeof(*p->children));p->child_count--;}
 o->animating=false;o->alive=false;count--;}
lv_obj_t *lv_label_create(lv_obj_t *p){lv_obj_t *o=lv_obj_create(p);o->label=true;o->h=24;return o;}
void lv_obj_remove_style_all(lv_obj_t *o){ACCESS();assert(o);}
int lv_color_hex(uint32_t c){return c;}
void lv_obj_set_style_bg_color(lv_obj_t *o,int v,int s){(void)s;ACCESS();assert(o);o->color=v;o->style_updates++;}
#define IGNORE_STYLE(fn) void fn(lv_obj_t *o,int v,int s){ACCESS();assert(o);o->style_updates++;(void)v;(void)s;}
IGNORE_STYLE(lv_obj_set_style_bg_opa)
IGNORE_STYLE(lv_obj_set_style_radius)
IGNORE_STYLE(lv_obj_set_style_border_color)
IGNORE_STYLE(lv_obj_set_style_border_width)
IGNORE_STYLE(lv_obj_set_style_translate_y)
IGNORE_STYLE(lv_obj_set_style_text_color)
IGNORE_STYLE(lv_obj_set_style_text_align)
void lv_obj_set_style_pad_all(lv_obj_t *o,int v,int s){ACCESS();(void)s;o->pad=v;}
void lv_obj_set_style_pad_hor(lv_obj_t *o,int v,int s){ACCESS();(void)o;(void)v;(void)s;}
void lv_obj_set_style_text_line_space(lv_obj_t *o,int v,int s){ACCESS();(void)s;o->line_space=v;}
void lv_obj_set_style_text_font(lv_obj_t *o,const lv_font_t *f,int s){ACCESS();(void)s;o->font=f;o->h=f->line_height;}
void lv_obj_set_hidden(lv_obj_t *o,bool en){ACCESS();o->hidden=en;}
void lv_obj_set_scrollable(lv_obj_t *o,bool en){ACCESS();(void)o;(void)en;}
void lv_obj_set_clickable(lv_obj_t *o,bool en){ACCESS();(void)o;(void)en;}
lv_obj_t *lv_obj_get_child(lv_obj_t *o,int idx){ACCESS();return idx>=0&&idx<o->child_count?o->children[idx]:NULL;}
static void place(lv_obj_t *o,int a,int x,int y);
void lv_obj_set_pos(lv_obj_t *o,int x,int y){ACCESS();place(o,o->alignment,x,y);}
void lv_obj_set_size(lv_obj_t *o,int w,int h){ACCESS();o->w=w;o->h=h;o->height_fixed=true;}
void lv_obj_set_width(lv_obj_t *o,int w){ACCESS();o->w=w;}
int lv_obj_get_width(lv_obj_t *o){ACCESS();return o->w;}
int32_t lv_obj_get_self_width(const lv_obj_t *o){ACCESS();int width=0;for(const unsigned char *p=(const unsigned char *)o->text;*p;p++){if((*p&0xc0)!=0x80)width+=*p<128?8:16;}return width;}
void lv_obj_set_height(lv_obj_t *o,int h){ACCESS();o->h=h;o->height_fixed=true;}
static void place(lv_obj_t *o,int a,int x,int y){
 int w=o->parent?o->parent->w:240,h=o->parent?o->parent->h:320;
 if(a==LV_ALIGN_TOP_RIGHT){x+=w-48;}
 else if(a==LV_ALIGN_LEFT_MID){y+=(h-o->h)/2;}
 else if(a==LV_ALIGN_RIGHT_MID){x+=w-o->w;y+=(h-o->h)/2;}
 else if(a==LV_ALIGN_TOP_MID){x+=(w-o->w)/2;}
 else if(a==LV_ALIGN_CENTER){x+=(w-o->w)/2;y+=(h-o->h)/2;}
 else if(a==LV_ALIGN_BOTTOM_LEFT){y+=h-o->h;}
 else if(a==LV_ALIGN_BOTTOM_MID){x+=(w-o->w)/2;y+=h-o->h;}
 o->x=x;o->y=y;
}
void lv_obj_align(lv_obj_t *o,int a,int x,int y){ACCESS();o->alignment=a;place(o,a,x,y);}
void lv_obj_set_scroll_dir(lv_obj_t *o,int d){ACCESS();o->dir=d;}
void lv_label_set_text(lv_obj_t *o,const char *s){ACCESS();assert(o&&o->alive&&s&&strlen(s)<sizeof(o->text));strcpy(o->text,s);o->text_updates++;}
void lv_label_set_text_static(lv_obj_t *o,const char *s){lv_label_set_text(o,s);}
const char *lv_label_get_text(lv_obj_t *o){ACCESS();return o->text;}
void lv_label_set_long_mode(lv_obj_t *o,int m){ACCESS();o->long_mode=m;o->long_mode_updates++;o->marquee_offset=0;}
lv_label_long_mode_t lv_label_get_long_mode(lv_obj_t *o){ACCESS();return o->long_mode;}
void lv_obj_update_layout(lv_obj_t *o){ACCESS();
 for(int i=0;i<o->child_count;i++){
  lv_obj_t *c=o->children[i];if(!c->label || c->height_fixed)continue;
  int chars=0,lines=1,column=0,per_line=c->w/12;if(per_line<1)per_line=1;
  for(const unsigned char *p=(const unsigned char *)c->text;*p;p++){
   if(*p=='\n'){lines++;column=0;continue;}if((*p&0xc0)==0x80)continue;
   chars++;if(++column>per_line){lines++;column=1;}
  }
  (void)chars;c->h=lines*((c->font?c->font->line_height:24)+c->line_space);
 }
}
int lv_obj_get_scroll_y(lv_obj_t *o){ACCESS();return o->scroll_y;}
int lv_obj_get_scroll_bottom(lv_obj_t *o){ACCESS();int max=0;for(int i=0;i<o->child_count;i++){int b=o->children[i]->y+o->children[i]->h;if(b>max)max=b;}int n=max+2*o->pad-o->h-o->scroll_y;return n>0?n:0;}
void lv_obj_scroll_to_y(lv_obj_t *o,int y,int a){ACCESS();(void)a;int max=o->scroll_y+lv_obj_get_scroll_bottom(o);if(y<0)y=0;if(y>max)y=max;o->scroll_y=y;}
bool lv_font_get_glyph_dsc(const lv_font_t *f,lv_font_glyph_dsc_t *g,uint32_t cp,uint32_t n){(void)n;assert(f);g->is_placeholder=false;return (cp>=32&&cp<127)||(cp>=0x4e00&&cp<=0x9fff&&cp!=0x9f98)||cp==0xb7||cp==0x2026||cp==0xff0c;}

void lv_anim_init(lv_anim_t *a){ACCESS();memset(a,0,sizeof(*a));}
void lv_anim_set_var(lv_anim_t *a,void *v){ACCESS();a->var=v;}
void lv_anim_set_values(lv_anim_t *a,int f,int l){ACCESS();a->first=f;a->last=l;}
void lv_anim_set_duration(lv_anim_t *a,uint32_t d){ACCESS();a->duration=d;}
void lv_anim_set_repeat_count(lv_anim_t *a,uint32_t c){ACCESS();a->repeat=c;}
void lv_anim_set_exec_cb(lv_anim_t *a,void (*f)(void *,int32_t)){ACCESS();a->exec=f;}
void lv_anim_set_completed_cb(lv_anim_t *a,void (*completed)(lv_anim_t *)){ACCESS();a->completed=completed;}
void lv_obj_set_style_anim(lv_obj_t *o,const lv_anim_t *a,int s){ACCESS();(void)s;o->animation_template=a;o->style_updates++;}
void lv_obj_set_style_anim_duration(lv_obj_t *o,uint32_t duration,int s){ACCESS();(void)s;o->anim_duration=(int)duration;o->style_updates++;}
uint16_t lv_font_get_glyph_width(const lv_font_t *f,uint32_t letter,uint32_t next){ACCESS();(void)f;(void)next;return letter<128?8:16;}
void lv_anim_start(lv_anim_t *a){ACCESS();lv_obj_t *o=a->var;o->animations_started++;o->anim_from=a->first;o->anim_to=a->last;o->anim_duration=a->duration;o->anim_repeat=a->repeat;o->animating=true;if(a->exec)a->exec(a->var,a->first);}
void lv_anim_delete(void *v,void (*f)(void *,int32_t)){ACCESS();(void)f;if(v)((lv_obj_t *)v)->animating=false;}
lv_obj_t *lv_arc_create(lv_obj_t *p){return lv_obj_create(p);}
void lv_arc_set_value(lv_obj_t *o,int v){ACCESS();assert(o);(void)v;}
void lv_arc_set_rotation(lv_obj_t *o,int r){ACCESS();assert(o);o->rotation=r;}
void lv_arc_set_range(lv_obj_t *o,int f,int l){ACCESS();assert(o);(void)f;(void)l;}
void lv_arc_set_bg_angles(lv_obj_t *o,int f,int l){ACCESS();assert(o);(void)f;(void)l;}
IGNORE_STYLE(lv_obj_set_style_arc_color)
IGNORE_STYLE(lv_obj_set_style_arc_opa)
IGNORE_STYLE(lv_obj_set_style_arc_width)
IGNORE_STYLE(lv_obj_set_style_text_decor)
void lv_screen_load(lv_obj_t *s){ACCESS();assert(s && s->alive);active=s;}
lv_obj_t *lv_screen_active(void){ACCESS();return active;}
