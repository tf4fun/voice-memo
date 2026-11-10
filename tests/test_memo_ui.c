#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "memo_ui_common.h"
#include "memo_ui_settings.h"
#include "memo_ui_detail.h"
#include "memo_follow.h"
#include "memo_font.h"
#include "memo_net.h"
#include "memo_discovery.h"
#include "memo_provision.h"
#include "memo_ui_provision.h"
static memo_settings_t settings={.volume=80,.brightness=100,.screen_timeout_s=30,.sleep_min=5,.tz_offset_min=480};
const memo_settings_t *memo_settings_get(void){return &settings;}
static bool online=true, discovery_pending;
static const char *network_ssid="网络SSID";
static memo_provision_state_t provision_state=MEMO_PROV_ADVERTISING;
bool memo_net_has_ip(void){return online;}
bool memo_upload_discovery_pending(void){return discovery_pending;}
memo_provision_state_t memo_provision_state(void){return provision_state;}
void memo_provision_info(memo_network_info_t *out){*out=(memo_network_info_t){.ip="192.168.1.2"};snprintf(out->ssid,sizeof(out->ssid),"%s",network_ssid);}
esp_err_t memo_provision_error(void){return ESP_FAIL;}
memo_net_state_t memo_net_state(void){return MEMO_NET_CONNECTED;}
void memo_net_info(memo_network_info_t *out){*out=(memo_network_info_t){.ssid="网络SSID"};}
bool memo_discovery_endpoint(memo_endpoint_t *out){*out=(memo_endpoint_t){.host="192.168.1.1",.port=8080};return true;}
memo_discovery_state_t memo_discovery_state(void){return MEMO_DISCOVERY_FOUND;}
static lv_obj_t *find(lv_obj_t *root,const char *text){if(!strcmp(root->text,text))return root;for(int i=0;i<root->child_count;i++){lv_obj_t *r=find(root->children[i],text);if(r)return r;}return NULL;}
static lv_obj_t *find_sized(lv_obj_t *root,int w,int children){if(root->w==w&&root->child_count==children)return root;for(int i=0;i<root->child_count;i++){lv_obj_t *r=find_sized(root->children[i],w,children);if(r)return r;}return NULL;}
static void visible_settings(int selected){
 memo_ui_settings_set_selected(selected);memo_ui_settings_refresh_values();
 lv_obj_t *root=memo_ui_settings_screen();int visible=0,highlight=0;
 for(int i=0;i<root->child_count;i++){
  lv_obj_t *row=root->children[i];if(row->child_count!=2||row->hidden)continue;
  visible++;assert(row->x==16 && row->y>=52 && row->y+row->h<=296);
  if(row->color==TERM_FG)highlight++;
 }
 assert(visible==6 && highlight==1);
 char position[16];snprintf(position,sizeof(position),"%d/9",selected+1);
 lv_obj_t *counter=find(root,position);assert(counter && counter->y+counter->h<=44);
}
int main(void){
 assert(memo_follow_target(0,0,100)==0);
 assert(memo_follow_target(UINT32_MAX,UINT32_MAX,UINT32_MAX)==UINT32_MAX-1);
 assert(memo_follow_target(500,1000,4096)==2048);
 assert(memo_follow_target(2000,1000,4096)==4095);
 assert(memo_follow_target(100,1000,0)==0);
 assert(memo_follow_scroll(12,12,12,100)==0);
 assert(memo_follow_scroll(12,12,13,100)==0);
 assert(memo_follow_scroll(200,100,300,1000)==502);
 assert(memo_follow_scroll(400,100,300,1000)==1000);

 memo_fonts_init();memo_ui_settings_build();memo_ui_settings_refresh();
 assert(find(memo_ui_settings_screen(),"> 设置")->y+find(memo_ui_settings_screen(),"> 设置")->h<=44);
 for(int i=0;i<9;i++)visible_settings(i);
 for(int i=8;i>=0;i--)visible_settings(i);
 lv_obj_t *netrow=NULL;
 for(int i=0;i<memo_ui_settings_screen()->child_count;i++){
  lv_obj_t *row=memo_ui_settings_screen()->children[i];
  if(row->child_count==2 && row->children[0]->label && strstr(row->children[0]->text,"网络")) netrow=row;
 }
 assert(netrow && strstr(netrow->children[0]->text,"网络"));
 assert(!strncmp(netrow->children[0]->text,"01 ",3));
 {int rules=0;for(int i=0;i<memo_ui_settings_screen()->child_count;i++){lv_obj_t *r=memo_ui_settings_screen()->children[i];if(r->h==1&&r->y==44&&r->w==208&&r->color==TERM_DARK)rules++;}assert(rules==1);}
 lv_obj_t *scroll=find_sized(memo_ui_settings_screen(),4,1);
 assert(scroll && !scroll->hidden);
 memo_ui_settings_set_selected(0);memo_ui_settings_refresh_values();
 const int thumb0=lv_obj_get_child(scroll,0)->y;
 memo_ui_settings_set_selected(6);memo_ui_settings_refresh_values();
 const int thumb6=lv_obj_get_child(scroll,0)->y;
 assert(thumb6>thumb0);
 memo_ui_settings_set_selected(0);memo_ui_settings_refresh_values();
 memo_ui_settings_set_selected(MEMO_SETTING_SERVER);
 memo_ui_settings_refresh_values();
 assert(find(memo_ui_settings_screen(),"192.168.1.1:8080"));
 lv_obj_t *address=find(memo_ui_settings_screen(),"192.168.1.1:8080");
 assert(address->w==88 && address->height_fixed && address->h==memo_font_label()->line_height);
 assert(address->long_mode==LV_LABEL_LONG_SCROLL_CIRCULAR);
 int mode_updates=address->long_mode_updates,text_updates=address->text_updates;
 address->marquee_offset=37;
 for(int i=0;i<30;i++)memo_ui_settings_refresh_values();
 assert(address->long_mode_updates==mode_updates && address->text_updates==text_updates && address->marquee_offset==37);
 memo_ui_settings_set_selected(MEMO_SETTING_VOLUME);memo_ui_settings_refresh_values();
 assert(address->long_mode==LV_LABEL_LONG_DOT && address->marquee_offset==0 && address->long_mode_updates==mode_updates+1);
 lv_obj_update_layout(address->parent);
 assert(address->h==memo_font_label()->line_height && address->y>=0 && address->y+address->h<=address->parent->h);
 memo_ui_settings_set_selected(MEMO_SETTING_SERVER);memo_ui_settings_refresh_values();
 assert(address->long_mode==LV_LABEL_LONG_SCROLL_CIRCULAR && address->long_mode_updates==mode_updates+2);
 address->marquee_offset=18;memo_ui_settings_refresh_values();assert(address->marquee_offset==18);
 memo_ui_settings_set_selected(MEMO_SETTING_ABOUT);memo_ui_settings_refresh_values();
 assert(address->long_mode==LV_LABEL_LONG_DOT && address->marquee_offset==0);
 memo_ui_settings_set_selected(MEMO_SETTING_SERVER);memo_ui_settings_refresh_values();
 assert(address->long_mode==LV_LABEL_LONG_SCROLL_CIRCULAR && address->marquee_offset==0);
 discovery_pending=true; memo_ui_settings_refresh_values();assert(find(memo_ui_settings_screen(),"查找中"));
 online=false;memo_ui_settings_refresh_values();assert(find(memo_ui_settings_screen(),"等待网络"));
 online=true;discovery_pending=false;
 memo_ui_settings_set_selected(MEMO_SETTING_AUTOSAVE);assert(find(memo_ui_settings_screen(),"08 自动保存"));
 memo_ui_settings_set_selected(MEMO_SETTING_VOLUME);
 memo_settings_t draft=settings;
 memo_ui_settings_enter(&draft);
 lv_obj_t *volume_track=find_sized(memo_ui_settings_screen(),160,1);
 assert(volume_track && !volume_track->hidden);
 lv_obj_t *volume_fill=volume_track->children[0];
 assert(volume_fill->w==124 && volume_fill->h==8 && volume_fill->x==1 && volume_fill->y==1);
 draft.volume=100;memo_ui_settings_show_draft(&draft);
 assert(volume_fill->animating && volume_fill->anim_from==124 && volume_fill->anim_to==156 && volume_fill->anim_duration==160);
 int animations=volume_fill->animations_started;
 for(int i=0;i<20;i++)memo_ui_settings_show_draft(&draft);
 assert(volume_fill->animations_started==animations);
 volume_fill->w=140;draft.volume=60;memo_ui_settings_show_draft(&draft);
 assert(volume_fill->anim_from==140 && volume_fill->anim_to==93);
 draft.volume=100;memo_ui_settings_show_draft(&draft);assert(memo_ui_settings_in_sheet());
 memo_ui_settings_refresh_values();assert(find(memo_ui_settings_screen(),"100%"));
 memo_ui_settings_error();lv_obj_t *error_hint=find(memo_ui_settings_screen(),"处理失败");
 assert(error_hint && !error_hint->hidden);
 memo_ui_settings_refresh();assert(!volume_fill->animating);assert(!memo_ui_settings_in_sheet()&&find(memo_ui_settings_screen(),"80%"));
 assert(error_hint->hidden && !error_hint->text[0]);
 memo_ui_settings_set_selected(MEMO_SETTING_BRIGHTNESS);
 draft=settings;memo_ui_settings_enter(&draft);
 assert(find(memo_ui_settings_screen(),"亮度") && !volume_track->hidden && volume_fill->w==156);
 draft.brightness=40;memo_ui_settings_show_draft(&draft);
 assert(volume_fill->animating && volume_fill->anim_to==62 && find(memo_ui_settings_screen(),"40%"));
 memo_ui_settings_refresh();assert(!volume_fill->animating);
 lv_obj_update_layout(memo_ui_settings_screen());
 for(int i=0;i<memo_ui_settings_screen()->child_count;i++){
  lv_obj_t *child=memo_ui_settings_screen()->children[i];
  if(child->label && strchr(child->text,'\n'))assert(child->y+child->h<=312);
 }
 memo_ui_detail_build();memo_ui_detail_show(0,3000,NULL);
 assert(find(memo_ui_detail_screen(),"--:-- / 0:03"));
 lv_obj_t *bar_track=find_sized(memo_ui_detail_screen(),56,1);
 assert(bar_track && bar_track->children[0]->color==TERM_FG);
 lv_obj_t *fill=bar_track->children[0];
 assert(fill->x==1 && fill->y==1 && fill->h==4);
 memo_ui_detail_set_playback(true,0,10000);assert(fill->w==0);
 memo_ui_detail_set_playback(true,5000,10000);assert(fill->w==26);
 memo_ui_detail_set_playback(true,10000,10000);assert(fill->w==52);
 memo_ui_detail_set_playback(true,20000,10000);assert(fill->w==52);
 assert(bar_track->parent->color==TERM_FG);
 memo_ui_detail_set_playback(false,0,0);assert(fill->w==0 && bar_track->parent->color==TERM_BG);
 memo_ui_detail_mode(MEMO_STATE_DETAIL,false,false);
 assert(!find(memo_ui_detail_screen(),"OK 播放 · 长按返回"));
 char text[2049];memset(text,'a',2048);text[2048]='\0';
 assert(memo_ui_detail_text(text,false));
 lv_obj_t *body=find(memo_ui_detail_screen(),text)->parent;
 assert(body->h==144 && body->dir==LV_DIR_VER);
 memo_ui_detail_follow(0,0,2048);assert(body->scroll_y==0);
 memo_ui_detail_follow(1024,0,2048);int middle=body->scroll_y;assert(middle>0);
 memo_ui_detail_follow(2047,0,2048);assert(body->scroll_y>middle && lv_obj_get_scroll_bottom(body)==0);
 memo_ui_detail_follow(0,0,2048);assert(body->scroll_y==0);
 assert(memo_ui_detail_text("中😀文",false));assert(find(memo_ui_detail_screen(),"中?文"));
 assert(memo_ui_detail_text("中\x80" "文",false));assert(find(memo_ui_detail_screen(),"中?文"));
 memo_ui_detail_mode(MEMO_STATE_PLAYING,false,false);assert(!find(memo_ui_detail_screen(),"OK 暂停 · 长按返回"));
 memo_ui_detail_mode(MEMO_STATE_PAUSED,false,false);assert(!find(memo_ui_detail_screen(),"OK 继续 · 长按返回"));
 memo_ui_detail_status(true,"");memo_ui_detail_mode(MEMO_STATE_DETAIL,false,false);
 assert(find(memo_ui_detail_screen(),"待传  音量 80%"));
 const struct {bool local,pending,error;const char *status,*label;} statuses[]={
  {true,false,false,"bad","录音损坏"},{true,false,false,"pending","待传"},
  {false,false,false,"failed","转写失败"},{false,false,false,"uploaded","待转写"},
  {false,false,false,"transcribing","转写中"},{false,false,false,"ready","正文"},
  {false,true,false,"ready","读取中"},{false,false,true,"ready","读取失败"},
 };
 settings.volume=100;
 for(size_t i=0;i<sizeof(statuses)/sizeof(statuses[0]);i++){
  memo_ui_detail_status(statuses[i].local,statuses[i].status);
  memo_ui_detail_mode(MEMO_STATE_DETAIL,statuses[i].pending,statuses[i].error);
  char expected[80];snprintf(expected,sizeof(expected),"%s  音量 100%%",statuses[i].label);
  lv_obj_t *status=find(memo_ui_detail_screen(),expected);
  assert(status && status->font==memo_font_label() && status->h==memo_font_label()->line_height);
  assert(body->y+body->h<status->y && status->y+status->h<bar_track->parent->y);
 }
 memo_ui_detail_message("音量未保存");
 assert(find(memo_ui_detail_screen(),"音量未保存"));
 memo_ui_detail_mode(MEMO_STATE_DETAIL,false,false);
 assert(!find(memo_ui_detail_screen(),"音量未保存"));
 settings.volume=80;
 memo_ui_provision_build();
 for(int state=MEMO_PROV_IDLE;state<=MEMO_PROV_FAILED;state++){
  provision_state=(memo_provision_state_t)state;memo_ui_provision_refresh();
 }
 provision_state=MEMO_PROV_WIFI_CONNECTING;memo_ui_provision_refresh();
 assert(find(memo_ui_provision_screen(),"SSID: 网络SSID"));
 provision_state=MEMO_PROV_WIFI_CONNECTED;memo_ui_provision_refresh();
 assert(find(memo_ui_provision_screen(),"SSID: 网络SSID") && find(memo_ui_provision_screen(),"IP: 192.168.1.2"));
 network_ssid="abcdefghijklmnopqrstuvwxyz123456";memo_ui_provision_refresh();
 lv_obj_t *ssid=find(memo_ui_provision_screen(),"SSID: abcdefghijklmnopqrstuvwxyz123456");
 assert(ssid && ssid->long_mode==LV_LABEL_LONG_SCROLL_CIRCULAR && ssid->y+ssid->h<=104);
 network_ssid="中😀文";memo_ui_provision_refresh();ssid=find(memo_ui_provision_screen(),"SSID: 中?文");
 assert(ssid);int updates=ssid->text_updates;memo_ui_provision_refresh();assert(ssid->text_updates==updates);
 memo_ui_battery_update(75);assert(find(memo_ui_provision_screen(),"75%"));assert(find(memo_ui_detail_screen(),"75%")&&find(memo_ui_settings_screen(),"75%"));
 memo_ui_battery_update(-1);assert(find(memo_ui_detail_screen(),"--"));
 puts("memo actual UI object bindings: settings viewport/draft, detail modes/scroll, dynamic glyph policy and shared battery: PASS");
}
