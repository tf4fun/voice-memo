// Execute the actual input loop, action executor and UI modules. Platform work
// is injected at the task boundaries; this is not a radio/audio/pixel simulation.
#include <assert.h>
#include <setjmp.h>
#include "memo_state.h"
static memo_action_t observed_handle(memo_sm_t *sm,memo_input_t input);
#define memo_sm_handle observed_handle
#include "../main/memo_app.c"
#undef memo_sm_handle
static unsigned actions[MEMO_ACT_COUNT];
static memo_action_t observed_handle(memo_sm_t *sm,memo_input_t input)
{
    memo_action_t action=memo_sm_handle(sm,input);
    actions[action]++;
    return action;
}


#include "nvs.h"
#include "nvs_flash.h"
static int64_t clock_us=1000000;
static bool in_button_callback, gpio_wake;
static int button_mv=3300;
static bool locked, deny_locks, online=true, rec_active, player_active, player_busy, player_paused;
static bool upload_running, events_running;
static unsigned rec_no_mem_count;
static esp_err_t network_start_error, upload_start_error, network_suspend_error;
static esp_err_t events_stop_error;
static bool release_during_stop;
static bool rec_prepared;
static int prepares, cancelled_prepares;
static int64_t prepare_delay_us;
static bool repress_during_prepare;
static bool recovery_blocked, capture_hold, playback_hold, discovery_requested, updated=true;
static int player_polls;
static int loop_budget, starts, stops, plays, pauses, resumes, text_requests, text_cancels, page_requests;
static int writes, commits, volume_calls, moves, removes, choices, discovery_requests, refresh_requests;
static int upload_stops, events_stops, network_stops, prov_starts, prov_stops, backlight;
static esp_err_t rec_start_error, rec_stop_error, player_stop_error, play_error, file_error, commit_error;
static esp_err_t provision_stop_error, upload_stop_error;
static uint32_t rec_elapsed, player_position;
static uint8_t signal_hint, rec_level=50;
static size_t free_bytes=1000000;
static memo_provision_state_t provision_state;
static memo_net_state_t net_state=MEMO_NET_CONNECTED;
static memo_api_page_t remote_page;
static memo_api_item_t remote_items[MEMO_API_MAX_ITEMS];
static memo_api_text_t remote_text;
static memo_spool_entry_t files[MEMO_SPOOL_MAX_ENTRIES];
static int file_count;
static memo_upload_rejection_t rejection;
static bool chosen_delete;
static char requested_id[37], requested_revision[65], played_source[100];
static uint32_t requested_offset;
static bool requested_previous;
static int requested_page;
static jmp_buf loop_exit;
static input_event_t queued[128];
static unsigned queued_count;
static void unlocked(void){assert(!locked && !in_button_callback);}
static void ui_access(void){assert(locked);}
QueueHandle_t xQueueCreate(unsigned count,unsigned size){assert(count==8 && size==sizeof(input_event_t));return (void *)1;}
int xQueueSend(QueueHandle_t q,const void *event,unsigned ticks){(void)ticks;assert(q && queued_count<8);queued[queued_count++]=*(const input_event_t *)event;return pdTRUE;}
int xQueueReceive(QueueHandle_t q,void *event,unsigned ticks){assert(q);if(loop_budget--<=0)longjmp(loop_exit,1);clock_us+=(int64_t)ticks*1000;if(!queued_count)return 0;*(input_event_t *)event=queued[0];memmove(queued,queued+1,--queued_count*sizeof(*queued));return pdTRUE;}
unsigned uxQueueMessagesWaiting(QueueHandle_t q){assert(q);return queued_count;}
int xTaskCreate(void (*entry)(void *),const char *name,unsigned stack,void *arg,unsigned priority,TaskHandle_t *out){(void)entry;(void)name;(void)stack;(void)arg;(void)priority;(void)out;return pdPASS;}
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t task){(void)task;return 8192;}
int64_t esp_timer_get_time(void){return clock_us;}
esp_err_t esp_timer_create(const esp_timer_create_args_t *a,esp_timer_handle_t *out){assert(a->callback);*out=(void *)2;return ESP_OK;}
esp_err_t esp_timer_start_once(esp_timer_handle_t t,uint64_t us){unlocked();assert(t && us>0 && us<=1000000);return ESP_OK;}
esp_err_t esp_timer_stop(esp_timer_handle_t t){unlocked();assert(t);return ESP_OK;}
size_t esp_get_free_heap_size(void){return 100000;}
size_t heap_caps_get_largest_free_block(unsigned c){(void)c;return 80000;}
void esp_fill_random(void *out,size_t len){memset(out,1,len);}
void esp_restart(void){assert(!"unexpected restart");}
uint32_t esp_sleep_get_wakeup_causes(void){return gpio_wake ? 1u<<ESP_SLEEP_WAKEUP_GPIO : 0;}
esp_err_t esp_deep_sleep_enable_gpio_wakeup(uint64_t mask,int mode){(void)mask;(void)mode;return ESP_OK;}
void esp_deep_sleep_start(void){assert(!"unexpected sleep");}
int bsp_button_read_mv(void){return button_mv;}
esp_err_t bsp_button_init(void (*cb)(bsp_btn_t,bsp_btn_ev_t,void *),void *a){(void)a;assert(cb==on_key);return ESP_OK;}
esp_err_t bsp_button_prepare_deep_sleep(int *level){*level=0;return ESP_OK;}
esp_err_t bsp_i2c_init(void){return ESP_OK;}
esp_err_t bsp_i2c_prepare_deep_sleep(void){return ESP_OK;}
esp_err_t bsp_audio_init(void){assert(s_list_screen==NULL && s_record_screen==NULL);return ESP_OK;}
esp_err_t bsp_audio_sleep(void){return ESP_OK;}
esp_err_t bsp_audio_prepare_deep_sleep(void){return ESP_OK;}
esp_err_t bsp_audio_set_format(uint32_t hz,uint8_t bits,uint8_t channels){(void)hz;(void)bits;(void)channels;return ESP_OK;}
void bsp_audio_set_volume(uint8_t v){unlocked();assert(v<=100);volume_calls++;}
esp_err_t bsp_battery_init(void){return ESP_OK;}
esp_err_t bsp_battery_sleep(void){return ESP_OK;}
int bsp_battery_soc(void){unlocked();return 75;}
esp_err_t bsp_display_init(void){return ESP_OK;}
void *bsp_lvgl_init(void){return (void *)3;}
bool bsp_lvgl_lock(int t){(void)t;assert(!locked);if(deny_locks)return false;locked=true;return true;}
void bsp_lvgl_unlock(void){assert(locked);locked=false;}
void bsp_display_backlight(uint8_t p){unlocked();backlight=p;}
esp_err_t bsp_display_prepare_deep_sleep(void){return ESP_OK;}
esp_err_t nvs_flash_init(void){unlocked();return ESP_OK;}
esp_err_t nvs_open(const char *name,int mode,nvs_handle_t *out){unlocked();(void)name;(void)mode;*out=1;return ESP_OK;}
esp_err_t nvs_get_i32(nvs_handle_t h,const char *k,int32_t *out){unlocked();(void)h;(void)k;(void)out;return ESP_ERR_NVS_NOT_FOUND;}
esp_err_t nvs_set_i32(nvs_handle_t h,const char *k,int32_t v){unlocked();(void)h;(void)k;(void)v;writes++;return ESP_OK;}
esp_err_t nvs_commit(nvs_handle_t h){unlocked();(void)h;commits++;return commit_error;}
esp_err_t memo_api_init(void){return ESP_OK;}
void memo_api_request_refresh(void){unlocked();refresh_requests++;}
bool memo_api_take_updated(void){bool value=updated;updated=false;return value;}
bool memo_api_page_copy(memo_api_item_t *items,size_t cap,memo_api_page_t *page){unlocked();assert(cap>= (size_t)remote_page.count);*page=remote_page;memcpy(items,remote_items,remote_page.count*sizeof(*items));return true;}
esp_err_t memo_api_request_page(int direction){unlocked();requested_page=direction;page_requests++;remote_page.loading=true;remote_page.error=ESP_OK;return ESP_OK;}
esp_err_t memo_api_request_text(const char *id,uint32_t off,bool previous,const char *rev){unlocked();text_requests++;strcpy(requested_id,id);strcpy(requested_revision,rev);requested_offset=off;requested_previous=previous;remote_text.pending=true;strcpy(remote_text.id,id);return ESP_OK;}
void memo_api_cancel_text(void){unlocked();text_cancels++;memset(&remote_text,0,sizeof(remote_text));}
bool memo_api_text_copy(memo_api_text_t *out){*out=remote_text;return true;}
esp_err_t memo_discovery_init(void){unlocked();return ESP_OK;}
void memo_discovery_stop(void){unlocked();}
memo_discovery_state_t memo_discovery_state(void){return MEMO_DISCOVERY_FOUND;}
bool memo_discovery_endpoint(memo_endpoint_t *out){*out=(memo_endpoint_t){.host="127.0.0.1",.port=8080,.confirmed=true};return true;}
esp_err_t memo_events_start(void){unlocked();events_running=true;return ESP_OK;}
esp_err_t memo_events_stop(void){unlocked();events_stops++;if(events_stop_error)return events_stop_error;events_running=false;return ESP_OK;}
bool memo_net_has_ip(void){return online;}
esp_err_t memo_net_start(void){unlocked();if(network_start_error){net_state=MEMO_NET_FAILED;return network_start_error;}net_state=MEMO_NET_CONNECTED;return ESP_OK;}
esp_err_t memo_net_suspend(void){unlocked();network_stops++;net_state=network_suspend_error?MEMO_NET_FAILED:MEMO_NET_OFF;return network_suspend_error;}
memo_net_state_t memo_net_state(void){return net_state;}
void memo_net_info(memo_network_info_t *out){*out=(memo_network_info_t){.ssid="test"};}
esp_err_t memo_provision_start(void){unlocked();prov_starts++;provision_state=MEMO_PROV_ADVERTISING;return ESP_OK;}
esp_err_t memo_provision_stop(void){unlocked();prov_stops++;if(!provision_stop_error)provision_state=MEMO_PROV_IDLE;return provision_stop_error;}
memo_provision_state_t memo_provision_state(void){return provision_state;}
void memo_provision_info(memo_network_info_t *out){*out=(memo_network_info_t){.ssid="test",.ip="127.0.0.1"};}
esp_err_t memo_provision_error(void){return ESP_FAIL;}
esp_err_t memo_recorder_init(void){return ESP_OK;}
esp_err_t memo_recorder_prepare(const char *id){unlocked();assert(s_sm.state==MEMO_STATE_ARMING && !events_running && !upload_running && capture_hold && !rec_prepared && !rec_active);
 if(!deny_locks)assert(s_ui_page==PAGE_ARMING && s_arm_arc->animating && s_arm_arc->anim_repeat==LV_ANIM_REPEAT_INFINITE);
 prepares++;clock_us+=prepare_delay_us;
 if(rec_no_mem_count){rec_no_mem_count--;return ESP_ERR_NO_MEM;}if(rec_start_error)return rec_start_error;
 rec_prepared=true;file_count=1;files[0]=(memo_spool_entry_t){.state=MEMO_SPOOL_RECORDING};strcpy(files[0].id,id);
 if(repress_during_prepare){repress_during_prepare=false;button_mv=3300;on_key(BSP_BTN_OK,BSP_BTN_RELEASE,NULL);button_mv=500;on_key(BSP_BTN_OK,BSP_BTN_PRESS,NULL);}
 return ESP_OK;}
esp_err_t memo_recorder_begin(void){unlocked();assert(rec_prepared && !rec_active && s_sm.state==MEMO_STATE_ARMING && clock_us-s_arming_started>=1000000);rec_prepared=false;rec_active=true;starts++;return ESP_OK;}
esp_err_t memo_recorder_cancel_prepared(void){unlocked();assert(rec_prepared && !rec_active);rec_prepared=false;file_count=0;cancelled_prepares++;return ESP_OK;}
esp_err_t memo_recorder_stop(bool keep,memo_recorder_result_t *out){unlocked();stops++;assert(keep);if(rec_stop_error){if(rec_stop_error!=ESP_ERR_TIMEOUT){rec_active=false;files[0].state=MEMO_SPOOL_DRAFT;files[0].duration_ms=rec_elapsed;}return rec_stop_error;}rec_active=false;assert(file_count==1);files[0].state=MEMO_SPOOL_DRAFT;files[0].duration_ms=rec_elapsed;*out=(memo_recorder_result_t){.duration_ms=rec_elapsed,.size_bytes=100};strcpy(out->id,files[0].id);return ESP_OK;}
bool memo_recorder_is_active(void){return rec_active;}
bool memo_recorder_is_busy(void){return rec_active||rec_prepared;}
uint32_t memo_recorder_elapsed_ms(void){return rec_elapsed;}
uint8_t memo_recorder_level(void){return rec_level;}
uint8_t memo_recorder_signal_hint(void){return signal_hint;}
esp_err_t memo_player_init(void){return ESP_OK;}
esp_err_t memo_player_stop(void){unlocked();player_active=player_paused=false;if(player_stop_error)return player_stop_error;player_busy=false;return ESP_OK;}
esp_err_t memo_player_play_file(const char *path){unlocked();plays++;player_position=0;if(play_error)return play_error;strcpy(played_source,path);player_active=player_busy=true;return ESP_OK;}
esp_err_t memo_player_play_remote(const char *id){unlocked();plays++;player_position=0;if(play_error)return play_error;strcpy(played_source,id);player_active=player_busy=true;return ESP_OK;}
esp_err_t memo_player_pause(void){unlocked();pauses++;player_paused=true;return ESP_OK;}
esp_err_t memo_player_resume(void){unlocked();resumes++;player_paused=false;return ESP_OK;}
bool memo_player_is_active(void){return player_active;}
bool memo_player_is_busy(void){return player_busy;}
bool memo_player_is_paused(void){return player_paused;}
void memo_player_poll(void){player_polls++;}
uint32_t memo_player_duration_ms(void){return 3000;}
uint32_t memo_player_position_ms(void){return player_position;}
size_t memo_spool_free_bytes(void){unlocked();return free_bytes;}
esp_err_t memo_spool_mount(void){unlocked();return ESP_OK;}
int memo_spool_recover(void){unlocked();return 0;}
bool memo_spool_recovery_blocked(void){return recovery_blocked;}
int memo_spool_scan(memo_spool_entry_t *out,size_t cap){unlocked();size_t n=(size_t)file_count<cap?(size_t)file_count:cap;if(out)memcpy(out,files,n*sizeof(*out));return file_count;}
esp_err_t memo_spool_move(const char *id,memo_spool_state_t from,memo_spool_state_t to){unlocked();moves++;assert(file_count==1 && !strcmp(id,files[0].id) && files[0].state==from);if(file_error)return file_error;files[0].state=to;return ESP_OK;}
esp_err_t memo_spool_remove(const char *id,memo_spool_state_t state){unlocked();removes++;assert(file_count==1 && !strcmp(id,files[0].id) && files[0].state==state);if(file_error)return file_error;file_count=0;return ESP_OK;}
esp_err_t memo_upload_start(void){unlocked();if(upload_start_error)return upload_start_error;upload_running=true;return ESP_OK;}
esp_err_t memo_upload_stop(void){unlocked();upload_stops++;if(release_during_stop)button_mv=3300;if(upload_stop_error)return upload_stop_error;upload_running=false;return ESP_OK;}
void memo_upload_set_capture_busy(bool busy){unlocked();capture_hold=busy;}
void memo_upload_set_playback_busy(bool busy){unlocked();playback_hold=busy;}
bool memo_upload_pending(void){return file_count>0 || recovery_blocked;}
bool memo_upload_rejection(memo_upload_rejection_t *out){*out=rejection;return out->id[0]!=0;}
esp_err_t memo_upload_choose(const char *id,bool deleting){unlocked();assert(!strcmp(id,rejection.id));choices++;chosen_delete=deleting;rejection.deciding=true;return ESP_OK;}
void memo_upload_request_discovery(void){unlocked();discovery_requests++;discovery_requested=true;}
bool memo_upload_discovery_pending(void){return discovery_requested;}

static void run(unsigned iterations)
{
    loop_budget=(int)iterations;
    if(setjmp(loop_exit)==0)input_task(NULL);
    assert(!locked);
}
static void queue_key(bsp_btn_t btn,bsp_btn_ev_t event){unlocked();if(event==BSP_BTN_PRESS)button_mv=btn==BSP_BTN_OK?500:btn==BSP_BTN_UP?0:300;else if(event==BSP_BTN_RELEASE)button_mv=3300;in_button_callback=true;on_key(btn,event,NULL);in_button_callback=false;}
static void key(bsp_btn_t btn,bsp_btn_ev_t event){queue_key(btn,event);run(1);}
static void click(bsp_btn_t btn){key(btn,BSP_BTN_PRESS);key(btn,BSP_BTN_RELEASE);key(btn,BSP_BTN_CLICK);}
static void hold(bsp_btn_t btn){key(btn,BSP_BTN_PRESS);key(btn,BSP_BTN_LONG);key(btn,BSP_BTN_RELEASE);}
static lv_obj_t *find(lv_obj_t *root,const char *text){if(!root)return NULL;if(!strcmp(root->text,text))return root;for(int i=0;i<root->child_count;i++){lv_obj_t *o=find(root->children[i],text);if(o)return o;}return NULL;}
static void remote_rows(int count)
{
    remote_page=(memo_api_page_t){.count=count};
    for(int i=0;i<count;i++){
        remote_items[i]=(memo_api_item_t){.recorded_at=1700000000,.duration_ms=3000,.status="ready",.snippet="test"};
        snprintf(remote_items[i].id,37,"00000000-0000-4000-8000-%012u",(unsigned)i+1);
    }
    updated=true;
}

static void list_select(int index)
{
    assert(s_sm.state==MEMO_STATE_LIST);
    while(s_selected>index)click(BSP_BTN_UP);
    while(s_selected<index)click(BSP_BTN_DOWN);
}
static void settings_select(int row)
{
    assert(s_sm.state==MEMO_STATE_SETTINGS);
    while(s_setting_selected>row)click(BSP_BTN_UP);
    while(s_setting_selected<row)click(BSP_BTN_DOWN);
}
static void heartbeat_cadence(void)
{
    // One uninterrupted task run: 50 ms receives must schedule 200 ms heartbeat.
    int before=player_polls;run(10);assert(player_polls==before+3);
}
static void test_settings_actions_survive_render_timeout(void)
{
    deny_locks=true;hold(BSP_BTN_UP);
    assert(s_sm.state==MEMO_STATE_SETTINGS);
    settings_select(MEMO_SETTING_SERVER);click(BSP_BTN_OK);
    assert(s_sm.state==MEMO_STATE_SETTINGS && discovery_requests==1);
    deny_locks=false;run(1);
    assert(find(memo_ui_settings_screen(),"查找中"));
    discovery_requested=false;run(1);
    lv_obj_t *server_value=find(memo_ui_settings_screen(),"127.0.0.1:8080");
    assert(server_value);
    int mode_updates=server_value->long_mode_updates;
    server_value->marquee_offset=20;run(12);
    assert(server_value->long_mode_updates==mode_updates && server_value->marquee_offset==20);
    settings_select(MEMO_SETTING_VOLUME);
    int before=writes;
    deny_locks=true;click(BSP_BTN_OK);click(BSP_BTN_UP);
    assert(s_settings_edit.active && s_settings_edit.value.volume==100 && writes==before);
    hold(BSP_BTN_OK);assert(s_sm.state==MEMO_STATE_SETTINGS && !s_settings_edit.active && writes==before);
    click(BSP_BTN_OK);click(BSP_BTN_DOWN);click(BSP_BTN_UP);click(BSP_BTN_UP);commit_error=ESP_FAIL;
    click(BSP_BTN_OK);assert(s_sm.state==MEMO_STATE_SUB_PAGE && s_settings_edit.active && memo_settings_get()->volume==80);
    int failed_writes=writes;
    deny_locks=false;run(3);
    assert(writes==failed_writes && find(memo_ui_settings_screen(),"处理失败"));
    commit_error=0;deny_locks=true;click(BSP_BTN_OK);
    assert(s_sm.state==MEMO_STATE_SETTINGS && !s_settings_edit.active && memo_settings_get()->volume==100);
    before=writes;deny_locks=false;run(4);assert(writes==before && !memo_ui_settings_in_sheet());
    settings_select(MEMO_SETTING_TIMEOUT);click(BSP_BTN_OK);click(BSP_BTN_DOWN);click(BSP_BTN_OK);
    assert(memo_settings_get()->screen_timeout_s==60);
    settings_select(MEMO_SETTING_SLEEP);click(BSP_BTN_OK);click(BSP_BTN_DOWN);hold(BSP_BTN_OK);
    assert(memo_settings_get()->sleep_min==5);
    settings_select(MEMO_SETTING_AUTOPLAY);deny_locks=true;click(BSP_BTN_OK);
    assert(memo_settings_get()->autoplay);before=writes;
    deny_locks=false;run(2);assert(writes==before);click(BSP_BTN_OK);assert(!memo_settings_get()->autoplay);
    heartbeat_cadence();
    settings_select(MEMO_SETTING_ABOUT);click(BSP_BTN_OK);assert(s_sm.state==MEMO_STATE_SETTINGS);
    hold(BSP_BTN_UP);assert(s_sm.state==MEMO_STATE_LIST);
}
static void page_complete(int count,int direction)
{
    remote_page.count=count;remote_page.loading=false;remote_page.error=ESP_OK;
    remote_page.navigation=direction;remote_page.generation++;updated=true;run(1);
}
static void test_list_and_detail_page_actions(void)
{
    list_select(11);strcpy(remote_page.next_cursor,"older");updated=true;run(1);
    int before=page_requests;click(BSP_BTN_DOWN);
    assert(page_requests==before+1 && requested_page==1 && s_page.loading);
    page_complete(3,1);assert(s_selected==0 && s_view_count==3);
    strcpy(remote_page.previous_cursor,"newer");remote_page.next_cursor[0]=0;updated=true;run(1);
    click(BSP_BTN_UP);assert(requested_page==-1);strcpy(remote_page.next_cursor,"older");page_complete(12,-1);
    assert(s_selected==11);
    // A failed page request preserves rows/selection and exposes the error.
    before=page_requests;click(BSP_BTN_DOWN);assert(page_requests==before+1);remote_page.loading=false;remote_page.error=ESP_FAIL;updated=true;run(1);
    assert(s_selected==11 && s_view_count==12 && find(s_list_screen,"读取失败"));
    remote_page.error=0;updated=true;run(1);
    // Background reorder follows the UUID, not the old row index.
    memo_api_item_t swap=remote_items[11];remote_items[11]=remote_items[1];remote_items[1]=swap;
    updated=true;run(1);assert(s_selected==1);
    deny_locks=true;click(BSP_BTN_OK);
    assert(s_sm.state==MEMO_STATE_DETAIL && !strcmp(requested_id,swap.id));
    int requests=text_requests;
    deny_locks=false;run(1);assert(memo_ui_detail_screen()!=NULL && text_requests==requests);
    // Detail keeps its UUID while metadata changes and volume keys never page.
    remote_items[1]=remote_items[0];updated=true;run(1);assert(!strcmp(s_detail_entry.id,swap.id));
    int selected=s_selected, pages=page_requests;
    click(BSP_BTN_DOWN);click(BSP_BTN_UP);
    assert(s_selected==selected && page_requests==pages && text_requests==requests);
    hold(BSP_BTN_OK);assert(s_sm.state==MEMO_STATE_LIST);
    remote_page=(memo_api_page_t){0};updated=true;run(1);click(BSP_BTN_OK);
    assert(s_sm.state==MEMO_STATE_LIST && s_view_count==0);
    remote_rows(12);updated=true;run(1);list_select(0);
}
static void window_complete(uint32_t offset,unsigned generation,char revision)
{
    remote_text=(memo_api_text_t){.offset=offset,.end=offset+2048,.total=4096,.generation=generation,.status="ready"};
    strcpy(remote_text.id,s_detail_entry.id);memset(remote_text.revision,revision,64);
    memset(remote_text.text,'a',2048);run(1);
}
static void test_text_and_player_actions(void)
{
    click(BSP_BTN_OK);assert(s_sm.state==MEMO_STATE_DETAIL);
    window_complete(0,1,'a');
    assert(!find(memo_ui_detail_screen(),"OK 播放 · 长按返回"));
    int before=text_requests, pages=page_requests, selected=s_selected;
    click(BSP_BTN_DOWN);assert(memo_settings_get()->volume==80);
    click(BSP_BTN_UP);assert(memo_settings_get()->volume==100);
    int persisted=writes;click(BSP_BTN_UP);assert(writes==persisted);
    for(int i=0;i<6;i++)click(BSP_BTN_DOWN);
    assert(memo_settings_get()->volume==0);
    persisted=writes;click(BSP_BTN_DOWN);assert(writes==persisted);
    commit_error=ESP_FAIL;click(BSP_BTN_UP);assert(memo_settings_get()->volume==0);
    assert(find(memo_ui_detail_screen(),"音量保存失败"));commit_error=0;
    click(BSP_BTN_UP);assert(memo_settings_get()->volume==20);
    assert(s_selected==selected && page_requests==pages && text_requests==before);
    // Conflict restarts at a valid UTF-8 boundary and pending work coalesces.
    remote_text.pending=false;remote_text.error=ESP_ERR_INVALID_STATE;run(1);
    assert(requested_offset==0 && !requested_previous && !requested_revision[0]);
    before=text_requests;run(3);assert(text_requests==before);
    window_complete(0,2,'b');heartbeat_cadence();
    play_error=ESP_FAIL;click(BSP_BTN_OK);assert(s_sm.state==MEMO_STATE_DETAIL && !player_busy);
    play_error=0;deny_locks=true;click(BSP_BTN_OK);assert(s_sm.state==MEMO_STATE_PLAYING && player_active);
    int played=plays;deny_locks=false;run(2);assert(plays==played);
    // Advance audio without keys: scroll within the window, then request next.
    player_position=750;run(1);
    lv_obj_t *body=find(memo_ui_detail_screen(),remote_text.text)->parent;
    assert(body->scroll_y>0);
    player_position=1700;run(1);assert(requested_offset==2048 && !requested_previous);
    before=text_requests;run(3);assert(text_requests==before);
    window_complete(2048,3,'b');assert(s_rendered_offset==2048);
    click(BSP_BTN_OK);assert(s_sm.state==MEMO_STATE_PAUSED && player_paused && pauses>0);
    int scroll=body->scroll_y;before=text_requests;
    click(BSP_BTN_DOWN);run(2);assert(body->scroll_y==scroll && text_requests==before && player_paused);
    click(BSP_BTN_OK);assert(s_sm.state==MEMO_STATE_PLAYING && !player_paused && resumes>0 && plays==played);
    player_position=3000;player_active=player_busy=false;run(1);
    assert(s_sm.state==MEMO_STATE_DETAIL);
    assert(bsp_lvgl_lock(1));assert(lv_obj_get_scroll_bottom(body)==0);bsp_lvgl_unlock();
    click(BSP_BTN_OK);run(1);assert(s_sm.state==MEMO_STATE_PLAYING && requested_offset==0);
    window_complete(0,4,'b');assert(body->scroll_y==0);
    int cancelled=text_cancels;hold(BSP_BTN_OK);
    assert(s_sm.state==MEMO_STATE_LIST && text_cancels==cancelled+1);
    assert(memo_settings_set_autoplay(true)==ESP_OK);click(BSP_BTN_OK);
    assert(s_sm.state==MEMO_STATE_PLAYING && player_active);
    hold(BSP_BTN_OK);assert(memo_settings_set_autoplay(false)==ESP_OK);
}

static void detail_page_complete(int first,int count,int direction,bool newer,bool older)
{
    const uint32_t generation=remote_page.generation+1;
    remote_rows(count);
    for(int i=0;i<count;i++)snprintf(remote_items[i].id,37,"00000000-0000-4000-8000-%012u",(unsigned)(first+i));
    remote_page.generation=generation;remote_page.navigation=direction;
    remote_page.loading=false;remote_page.error=ESP_OK;
    if(newer)strcpy(remote_page.previous_cursor,"newer");
    if(older)strcpy(remote_page.next_cursor,"older");
    updated=true;run(1);
}
static void detail_is(int id)
{
    char expected[37];snprintf(expected,sizeof(expected),"00000000-0000-4000-8000-%012u",(unsigned)id);
    assert(!strcmp(s_detail_entry.id,expected));
}
static void test_detail_adjacent_navigation(void)
{
    assert(s_sm.state==MEMO_STATE_LIST && !file_count);
    detail_page_complete(1,3,2,false,true);list_select(1);click(BSP_BTN_OK);detail_is(2);
    const int volume=memo_settings_get()->volume;
    hold(BSP_BTN_DOWN);detail_is(3);hold(BSP_BTN_UP);detail_is(2);
    click(BSP_BTN_OK);assert(player_active);
    hold(BSP_BTN_DOWN);detail_is(3);assert(!player_active && !player_busy);
    hold(BSP_BTN_UP);click(BSP_BTN_OK);click(BSP_BTN_OK);assert(player_paused);
    hold(BSP_BTN_UP);detail_is(1);assert(!player_active && !player_busy);
    int pages=page_requests;hold(BSP_BTN_UP);detail_is(1);
    assert(page_requests==pages && find(memo_ui_detail_screen(),"已是第一条"));
    hold(BSP_BTN_DOWN);detail_is(2);click(BSP_BTN_OK);
    player_stop_error=ESP_ERR_TIMEOUT;hold(BSP_BTN_DOWN);detail_is(2);
    assert(player_busy && find(memo_ui_detail_screen(),"音频忙"));
    player_stop_error=0;assert(memo_player_stop()==ESP_OK);
    hold(BSP_BTN_DOWN);detail_is(3);
    hold(BSP_BTN_DOWN);assert(s_detail_navigation.pending && requested_page==1);detail_is(3);
    int playback=plays;click(BSP_BTN_OK);assert(plays==playback);
    key(BSP_BTN_OK,BSP_BTN_PRESS); // A click begun during loading stays consumed.
    detail_page_complete(4,3,1,true,false);detail_is(4);
    key(BSP_BTN_OK,BSP_BTN_RELEASE);key(BSP_BTN_OK,BSP_BTN_CLICK);assert(plays==playback);
    assert(!s_detail_navigation.pending && s_sm.state==MEMO_STATE_DETAIL);
    hold(BSP_BTN_DOWN);detail_is(5);hold(BSP_BTN_DOWN);detail_is(6);
    pages=page_requests;hold(BSP_BTN_DOWN);detail_is(6);
    assert(page_requests==pages && find(memo_ui_detail_screen(),"已是最后一条"));
    hold(BSP_BTN_UP);hold(BSP_BTN_UP);detail_is(4);
    hold(BSP_BTN_UP);assert(s_detail_navigation.pending && requested_page==-1);
    detail_page_complete(1,3,-1,false,true);detail_is(3);
    hold(BSP_BTN_DOWN);assert(s_detail_navigation.pending);
    remote_page.loading=false;remote_page.error=ESP_FAIL;updated=true;run(1);
    assert(!s_detail_navigation.pending && find(memo_ui_detail_screen(),"切换失败"));detail_is(3);
    hold(BSP_BTN_DOWN);assert(s_detail_navigation.pending);
    hold(BSP_BTN_OK);assert(s_sm.state==MEMO_STATE_LIST && !s_detail_navigation.pending);
    detail_page_complete(7,3,1,true,false);assert(s_sm.state==MEMO_STATE_LIST);
    assert(memo_settings_get()->volume==volume);
    detail_page_complete(1,12,2,false,true);
}
static void test_detail_local_navigation_ownership(void)
{
    assert(s_sm.state==MEMO_STATE_LIST && !file_count);
    file_count=1;files[0]=(memo_spool_entry_t){.state=MEMO_SPOOL_PENDING,.duration_ms=3000};
    strcpy(files[0].id,"11111111-1111-4111-8111-111111111111");
    detail_page_complete(1,3,2,false,true);list_select(0);click(BSP_BTN_OK);click(BSP_BTN_OK);
    assert(s_local_playback_held && playback_hold && player_active);
    hold(BSP_BTN_DOWN);detail_is(1);
    assert(!s_local_playback_held && !playback_hold && !player_busy && file_count==1);
    hold(BSP_BTN_UP);assert(s_detail_entry.local && !strcmp(s_detail_entry.id,files[0].id));
    hold(BSP_BTN_OK);
    detail_page_complete(4,3,1,true,false);list_select(0);click(BSP_BTN_OK);
    hold(BSP_BTN_DOWN);assert(s_detail_navigation.pending && requested_page==0);
    detail_page_complete(1,3,2,false,true);detail_is(1);assert(!s_detail_entry.local);
    hold(BSP_BTN_OK);file_count=0;
    detail_page_complete(1,12,2,false,true);
}

static void test_failed_list_page_does_not_poison_detail(void)
{
    player_position=2000; // Previous completed recording must not leak into a new detail.
    list_select(11);strcpy(remote_page.next_cursor,"older");updated=true;run(1);
    click(BSP_BTN_DOWN);remote_page.loading=false;remote_page.error=ESP_FAIL;updated=true;run(1);
    click(BSP_BTN_OK);
    remote_text=(memo_api_text_t){.generation=1,.text="test",.revision="r",.end=4,.total=4,.status="ready"};
    strcpy(remote_text.id,s_detail_entry.id);run(1);
    assert(!find(memo_ui_detail_screen(),"OK 播放 · 长按返回"));
    assert(find(memo_ui_detail_screen(),"> 0:00/0:03"));
    char retained_id[37];strcpy(retained_id,s_detail_entry.id);
    int before=page_requests;click(BSP_BTN_DOWN);
    assert(!strcmp(s_detail_entry.id,retained_id) && page_requests==before);
    hold(BSP_BTN_OK);remote_rows(12);remote_page.error=0;updated=true;run(1);list_select(0);
}
static void begin_capture(void)
{
    assert(s_sm.state==MEMO_STATE_LIST && !file_count);
    key(BSP_BTN_OK,BSP_BTN_PRESS);assert(s_sm.state==MEMO_STATE_ARMING);
    clock_us+=1000000;arm_timer_cb(NULL);run(1);
    assert(s_sm.state==MEMO_STATE_RECORDING && rec_active && capture_hold);
    if(!deny_locks){
        assert(find(s_record_screen,"> 录音")==s_rec_title && s_rec_title->font==memo_font_label());
        assert(s_rec_title->y+s_rec_title->h<=41);
        assert(s_capture_caption->font==memo_font_label() && s_capture_caption->y+s_capture_caption->h<=s_timer_label->y);
        assert(s_input_caption->font==memo_font_label() && s_input_caption->y+s_input_caption->h<=258);
    }
}
static void test_capture_gesture_and_presentation(void)
{
    int reads=text_requests, captures=starts;
    key(BSP_BTN_OK,BSP_BTN_PRESS);
    assert(s_sm.state==MEMO_STATE_ARMING && !s_arming_visible);
    assert(bsp_lvgl_lock(1));assert(lv_screen_active()==s_list_screen);bsp_lvgl_unlock();
    run(3);
    assert(s_arming_visible && s_capture_hold.consume_click);
    assert(find(s_arming_screen,"> 准备")->font==memo_font_label());
    assert(s_arm_arc->parent==s_arming_screen && s_record_screen==NULL && s_timer_label==NULL);
    assert(!find(s_arming_screen,"00:00") && !find(s_arming_screen,"输入电平"));
    assert(bsp_lvgl_lock(1));assert(lv_screen_active()==s_arming_screen);bsp_lvgl_unlock();
    key(BSP_BTN_OK,BSP_BTN_RELEASE);key(BSP_BTN_OK,BSP_BTN_CLICK);
    key(BSP_BTN_OK,BSP_BTN_CLICK); // Duplicate click tails remain consumed.
    assert(s_sm.state==MEMO_STATE_LIST && text_requests==reads && starts==captures);
    click(BSP_BTN_OK);assert(s_sm.state==MEMO_STATE_DETAIL && text_requests>reads);
    hold(BSP_BTN_OK);

    // A short tap queued behind work must still open detail, without briefly
    // showing/committing preparation based on delayed dispatch time.
    reads=text_requests;
    queue_key(BSP_BTN_OK,BSP_BTN_PRESS);clock_us+=50000;
    queue_key(BSP_BTN_OK,BSP_BTN_RELEASE);queue_key(BSP_BTN_OK,BSP_BTN_CLICK);
    clock_us+=500000;run(3);
    assert(s_sm.state==MEMO_STATE_DETAIL && text_requests>reads && !s_capture_hold.consume_click);
    hold(BSP_BTN_OK);

    // Cancellation is committed even when the display lock never succeeds.
    reads=text_requests;deny_locks=true;
    key(BSP_BTN_OK,BSP_BTN_PRESS);clock_us+=200000;
    key(BSP_BTN_OK,BSP_BTN_RELEASE);key(BSP_BTN_OK,BSP_BTN_CLICK);
    deny_locks=false;run(1);
    assert(s_sm.state==MEMO_STATE_LIST && text_requests==reads && starts==captures);

    key(BSP_BTN_OK,BSP_BTN_PRESS);enqueue_input(MEMO_IN_ARM_COMPLETE);run(1);
    assert(s_sm.state==MEMO_STATE_ARMING && starts==captures); // stale timer
    key(BSP_BTN_OK,BSP_BTN_RELEASE);
    begin_capture();
    rec_level=100;run(1);
    assert(s_level_bars[VM_LEVEL_BARS-1]->h==56);
    rec_level=0;run(1);
    assert(s_level_bars[VM_LEVEL_BARS-1]->h==2 && s_level_bars[VM_LEVEL_BARS-2]->h==56);
    assert(s_timer_label->y+s_timer_label->h<160);
    for(int i=0;i<VM_LEVEL_BARS;i++) {
        assert(!s_level_bars[i]->hidden && s_level_bars[i]->y>=160 && s_level_bars[i]->y+s_level_bars[i]->h<=216);
    }
    rec_elapsed=1200;key(BSP_BTN_OK,BSP_BTN_RELEASE);
    key(BSP_BTN_OK,BSP_BTN_CLICK);
    assert(s_sm.state==MEMO_STATE_CONFIRM && file_count==1);
    assert(s_wave_baseline->hidden && s_input_level->hidden && s_arm_arc==NULL && s_arming_screen==NULL);
    assert(find(s_record_screen,"> 保存")==s_rec_title && find(s_record_screen,"录音完成")==s_capture_caption);
    assert(s_record_hint->y==154 && s_record_hint->y+s_record_hint->h<s_sheet->y);
    for(int i=0;i<VM_LEVEL_BARS;i++)assert(s_level_bars[i]->hidden);
    hold(BSP_BTN_OK);assert(s_sm.state==MEMO_STATE_LIST && !file_count);
    rec_level=50;
}
static void test_capture_draft_and_local_reader(void)
{
    int before=starts;key(BSP_BTN_OK,BSP_BTN_PRESS);key(BSP_BTN_OK,BSP_BTN_RELEASE);
    assert(s_sm.state==MEMO_STATE_LIST && starts==before); // cancelled arming
    online=false;deny_locks=true;begin_capture();deny_locks=false;run(1);
    rec_elapsed=3000;key(BSP_BTN_OK,BSP_BTN_RELEASE);
    assert(s_sm.state==MEMO_STATE_CONFIRM && capture_hold && files[0].state==MEMO_SPOOL_DRAFT);
    assert(find(s_record_screen,"0:03") || find(s_record_screen,"00:03"));
    file_error=ESP_FAIL;click(BSP_BTN_OK);
    assert(s_sm.state==MEMO_STATE_CONFIRM && capture_hold && files[0].state==MEMO_SPOOL_DRAFT);
    assert(find(s_record_screen,"保存失败 · 请重试"));
    int failed=moves;deny_locks=true;file_error=0;click(BSP_BTN_OK);
    assert(s_sm.state==MEMO_STATE_LIST && !capture_hold && files[0].state==MEMO_SPOOL_PENDING && moves==failed+1);
    deny_locks=false;run(2);assert(moves==failed+1);
    before=starts;deny_locks=true;key(BSP_BTN_OK,BSP_BTN_PRESS);
    assert(s_sm.state==MEMO_STATE_LIST && starts==before);
    deny_locks=false;run(1);assert(find(s_list_screen,"上一条待传"));
    key(BSP_BTN_OK,BSP_BTN_RELEASE);clock_us+=3000000;run(1);
    assert(s_list_hint->text[0]==0);
    online=true;
    // Acknowledged remotely but still retained as a local playback source.
    strcpy(files[0].id,remote_items[0].id);updated=true;run(1);
    assert(s_local_count==1 && s_view_count==remote_page.count && s_view[0].local);
    for(int i=1;i<s_view_count;i++)assert(strcmp(s_view[0].id,s_view[i].id));
    list_select(0);before=text_requests;click(BSP_BTN_OK);
    assert(s_sm.state==MEMO_STATE_DETAIL && text_requests==before && s_detail_entry.local);
    click(BSP_BTN_OK);
    assert(s_sm.state==MEMO_STATE_PLAYING && playback_hold && s_local_playback_held && strstr(played_source,".memo"));
    click(BSP_BTN_OK);assert(s_sm.state==MEMO_STATE_PAUSED && playback_hold && file_count==1);
    player_stop_error=ESP_ERR_TIMEOUT;hold(BSP_BTN_OK);
    assert(s_sm.state==MEMO_STATE_LIST && playback_hold && player_busy);
    player_stop_error=0;assert(memo_player_stop()==ESP_OK);run(1);
    assert(!playback_hold && !s_local_playback_held && file_count==1);
    // Worker acceptance releases the file; same UUID remains as a remote row.
    file_count=0;updated=true;run(1);assert(s_local_count==0 && !s_view[0].local);

    begin_capture();online=false;assert(rec_active);rec_elapsed=3000;rec_stop_error=ESP_ERR_TIMEOUT;
    key(BSP_BTN_OK,BSP_BTN_RELEASE);assert(s_sm.state==MEMO_STATE_RECORDING && capture_hold);
    rec_stop_error=0;key(BSP_BTN_OK,BSP_BTN_RELEASE);assert(s_sm.state==MEMO_STATE_CONFIRM);
    click(BSP_BTN_DOWN);assert(!s_sm.confirm_save);file_error=ESP_FAIL;click(BSP_BTN_OK);
    assert(s_sm.state==MEMO_STATE_CONFIRM && file_count==1 && capture_hold);
    file_error=0;hold(BSP_BTN_OK);assert(s_sm.state==MEMO_STATE_LIST && file_count==0 && !capture_hold);

    online=true;
    // Recovery restores confirmation, never uploads an unconfirmed draft.
    file_count=1;files[0]=(memo_spool_entry_t){.state=MEMO_SPOOL_DRAFT,.duration_ms=4200};
    strcpy(files[0].id,remote_items[0].id);run(1);
    assert(s_sm.state==MEMO_STATE_CONFIRM && s_confirm_ms==4200 && capture_hold && s_sm.confirm_save);
    hold(BSP_BTN_OK);assert(file_count==0 && s_sm.state==MEMO_STATE_LIST);

    rec_start_error=ESP_FAIL;key(BSP_BTN_OK,BSP_BTN_PRESS);clock_us+=1000000;arm_timer_cb(NULL);run(1);
    assert(s_sm.state==MEMO_STATE_LIST && !capture_hold && find(s_list_screen,"录音不可用"));
    int tail_reads=text_requests;key(BSP_BTN_OK,BSP_BTN_RELEASE);key(BSP_BTN_OK,BSP_BTN_CLICK);
    assert(s_sm.state==MEMO_STATE_LIST && text_requests==tail_reads);rec_start_error=0;
    begin_capture();rec_elapsed=MEMO_RECORD_LIMIT_MS;run(2);
    assert(s_sm.state==MEMO_STATE_CONFIRM && !rec_active && s_confirm_ms==MEMO_RECORD_LIMIT_MS);
    hold(BSP_BTN_OK);assert(!file_count && s_sm.state==MEMO_STATE_LIST);
}

static void test_low_memory_capture_restores_all_network_services(void)
{
    rec_no_mem_count=1;begin_capture();
    assert(s_rec_network_suspended && net_state==MEMO_NET_OFF && !upload_running && !events_running);
    rec_elapsed=3000;rec_stop_error=ESP_FAIL;key(BSP_BTN_OK,BSP_BTN_RELEASE);
    assert(!s_rec_network_suspended && net_state==MEMO_NET_CONNECTED && upload_running && events_running);
    rec_stop_error=0;run(1);assert(s_sm.state==MEMO_STATE_CONFIRM);
    hold(BSP_BTN_OK);assert(file_count==0);

    rec_no_mem_count=1;begin_capture();network_start_error=ESP_FAIL;
    key(BSP_BTN_OK,BSP_BTN_RELEASE);
    assert(s_sm.state==MEMO_STATE_CONFIRM && s_rec_workers_paused && net_state==MEMO_NET_OFF);
    hold(BSP_BTN_OK);assert(file_count==0 && s_network_resume_needed && net_state==MEMO_NET_FAILED);
    network_start_error=0;upload_start_error=ESP_ERR_NO_MEM;run(1);
    assert(s_network_resume_needed && net_state==MEMO_NET_CONNECTED && !upload_running);
    upload_start_error=0;run(1);
    assert(!s_network_resume_needed && upload_running && events_running);

    rec_no_mem_count=1;rec_start_error=ESP_FAIL;
    key(BSP_BTN_OK,BSP_BTN_PRESS);clock_us+=1000000;arm_timer_cb(NULL);run(1);
    assert(s_sm.state==MEMO_STATE_LIST && !file_count && !capture_hold && !s_rec_network_suspended);
    assert(net_state==MEMO_NET_CONNECTED && upload_running && events_running && !s_network_resume_needed);
    rec_start_error=0;key(BSP_BTN_OK,BSP_BTN_RELEASE);
}
static void test_failed_driver_release_does_not_retry_capture(void)
{
    rec_no_mem_count=1;network_suspend_error=network_start_error=ESP_FAIL;
    int before=prepares;
    key(BSP_BTN_OK,BSP_BTN_PRESS);clock_us+=1000000;arm_timer_cb(NULL);run(1);
    assert(prepares==before+1 && !file_count && !rec_active && !capture_hold);
    assert(s_sm.state==MEMO_STATE_LIST && !s_rec_network_suspended && s_network_resume_needed);
    network_suspend_error=network_start_error=0;run(1);
    assert(!s_network_resume_needed && upload_running && events_running && net_state==MEMO_NET_CONNECTED);
    key(BSP_BTN_OK,BSP_BTN_RELEASE);
}

static void test_rejection_choices_and_lock_retry(void)
{
    const int baseline=ui_test_live_objects();
    rejection=(memo_upload_rejection_t){.duration_ms=3000};strcpy(rejection.id,remote_items[0].id);
    deny_locks=true;run(1);assert(s_sm.state==MEMO_STATE_LIST);
    deny_locks=false;run(1);assert(s_sm.state==MEMO_STATE_UPLOAD_REJECTED && !s_sm.reject_delete);
    click(BSP_BTN_DOWN);assert(s_sm.reject_delete);
    int before=choices;deny_locks=true;hold(BSP_BTN_OK);
    assert(choices==before+1 && !chosen_delete && s_reject_deciding);
    deny_locks=false;run(1);click(BSP_BTN_OK);assert(choices==before+1);
    rejection.deciding=false;rejection.operation_failed=true;run(1);click(BSP_BTN_DOWN);click(BSP_BTN_OK);
    assert(choices==before+2 && chosen_delete);
    memset(&rejection,0,sizeof(rejection));deny_locks=true;run(1);
    assert(s_sm.state==MEMO_STATE_UPLOAD_REJECTED);
    deny_locks=false;run(1);assert(s_sm.state==MEMO_STATE_LIST && s_reject_sheet==NULL);
    assert(ui_test_live_objects()==baseline);
    assert(!s_rows[0]->hidden); // Closing the choice restores rows in this render.
}
static void test_provision_actions_and_wake_gesture(void)
{
    hold(BSP_BTN_UP);settings_select(MEMO_SETTING_NETWORK);
    upload_stop_error=ESP_ERR_TIMEOUT;int before=prov_starts;
    click(BSP_BTN_OK);assert(s_sm.state==MEMO_STATE_SETTINGS && prov_starts==before);
    upload_stop_error=0;network_suspend_error=network_start_error=ESP_FAIL;
    click(BSP_BTN_OK);
    assert(s_sm.state==MEMO_STATE_SETTINGS && s_handoff.phase==MEMO_HANDOFF_RESUMING && prov_starts==before);
    click(BSP_BTN_OK);assert(prov_starts==before);
    network_suspend_error=network_start_error=0;run(1);
    assert(s_handoff.phase==MEMO_HANDOFF_NORMAL);
    deny_locks=true;click(BSP_BTN_OK);
    assert(s_sm.state==MEMO_STATE_PROVISIONING && prov_starts==before+1 && network_stops>0);
    deny_locks=false;run(1);assert(find(memo_ui_provision_screen(),"等待手机连接"));
    provision_stop_error=ESP_ERR_TIMEOUT;hold(BSP_BTN_UP);
    assert(s_sm.state==MEMO_STATE_PROVISIONING && s_handoff.phase==MEMO_HANDOFF_EXITING);
    provision_stop_error=0;run(1);assert(s_sm.state==MEMO_STATE_SETTINGS && s_handoff.phase==MEMO_HANDOFF_NORMAL);
    click(BSP_BTN_OK);provision_state=MEMO_PROV_WIFI_CONNECTED;run(1);
    clock_us+=2000000;run(1);assert(s_sm.state==MEMO_STATE_SETTINGS);
    click(BSP_BTN_OK);provision_state=MEMO_PROV_FAILED;run(1);assert(s_sm.state==MEMO_STATE_SETTINGS);
    hold(BSP_BTN_UP);assert(s_sm.state==MEMO_STATE_LIST);
    // Keep awake and consume the entire first physical wake gesture.
    int selected=s_selected;clock_us+=61000000;run(1);
    assert(s_power.blank && !s_backlight_on && backlight==0);
    click(BSP_BTN_DOWN);assert(s_selected==selected && s_backlight_on && backlight==100);
    click(BSP_BTN_DOWN);assert(s_selected==selected+1);
}
static void test_inspiration_capture_and_sleep(void)
{
    assert(s_sm.state==MEMO_STATE_LIST && file_count==0);
    free_bytes=0;int before=starts;key(BSP_BTN_OK,BSP_BTN_PRESS);
    assert(s_sm.state==MEMO_STATE_LIST && starts==before && find(s_list_screen,"空间不足"));
    key(BSP_BTN_OK,BSP_BTN_RELEASE);free_bytes=1000000;
    // A dropped release cannot turn the arming timer into hands-free capture.
    key(BSP_BTN_OK,BSP_BTN_PRESS);button_mv=3300;clock_us+=1000000;arm_timer_cb(NULL);run(1);
    assert(s_sm.state==MEMO_STATE_LIST && starts==before);
    key(BSP_BTN_OK,BSP_BTN_RELEASE);
    // Ordinary short wake still only lights the screen; a held wake records.
    s_power.blank=true;s_backlight_on=false;
    int reads=text_requests;click(BSP_BTN_OK);
    assert(s_sm.state==MEMO_STATE_LIST && text_requests==reads && starts==before);
    s_power.blank=true;s_backlight_on=false;
    key(BSP_BTN_OK,BSP_BTN_PRESS);assert(s_sm.state==MEMO_STATE_ARMING);
    clock_us+=1000000;arm_timer_cb(NULL);run(1);
    assert(s_sm.state==MEMO_STATE_RECORDING && starts==before+1);
    signal_hint=MEMO_SIGNAL_QUIET;run(1);assert(find(s_record_screen,"声音偏小"));
    assert(s_record_hint->y==270 && s_record_hint->y+s_record_hint->h<=312);
    signal_hint=MEMO_SIGNAL_CLIPPED;run(1);assert(find(s_record_screen,"声音过大"));
    signal_hint=MEMO_SIGNAL_NORMAL;run(1);assert(s_record_hint->text[0]==0);
    key(BSP_BTN_OK,BSP_BTN_RELEASE);assert(s_sm.state==MEMO_STATE_CONFIRM);
    key(BSP_BTN_OK,BSP_BTN_CLICK);assert(s_sm.state==MEMO_STATE_CONFIRM); // wake tail
    hold(BSP_BTN_OK);assert(s_sm.state==MEMO_STATE_LIST && file_count==0);

    hold(BSP_BTN_UP);settings_select(MEMO_SETTING_AUTOSAVE);
    commit_error=ESP_FAIL;click(BSP_BTN_OK);assert(!memo_settings_get()->autosave && s_settings_error);
    commit_error=0;click(BSP_BTN_OK);assert(memo_settings_get()->autosave);
    hold(BSP_BTN_UP);
    begin_capture();deny_locks=true;key(BSP_BTN_OK,BSP_BTN_RELEASE);
    assert(s_sm.state==MEMO_STATE_AUTOSAVE && !s_autosave_until && capture_hold);
    assert(files[0].state==MEMO_SPOOL_DRAFT);
    clock_us+=5000000;run(1);assert(s_sm.state==MEMO_STATE_AUTOSAVE && capture_hold);
    deny_locks=false;run(1);assert(s_autosave_until && find(s_list_screen,"已保存 · 可撤销"));
    click(BSP_BTN_OK);assert(s_sm.state==MEMO_STATE_LIST && !file_count && !capture_hold);
    clock_us+=4000000;run(1);assert(!file_count);

    begin_capture();key(BSP_BTN_OK,BSP_BTN_RELEASE);
    file_error=ESP_FAIL;clock_us+=3000000;run(1);
    assert(s_sm.state==MEMO_STATE_CONFIRM && capture_hold && files[0].state==MEMO_SPOOL_DRAFT);
    assert(find(s_record_screen,"保存失败 · 请重试"));
    file_error=0;click(BSP_BTN_OK);assert(s_sm.state==MEMO_STATE_LIST && !capture_hold);
    assert(files[0].state==MEMO_SPOOL_PENDING);
    // Pending files are safe after worker join; drafts/recovery faults are not.
    s_sleep_files_checked=false;assert(power_safe(NULL));
    assert(bsp_lvgl_lock(1));assert(power_safe(NULL));bsp_lvgl_unlock();
    files[0].state=MEMO_SPOOL_DRAFT;s_sleep_files_checked=false;assert(!power_safe(NULL));
    files[0].state=MEMO_SPOOL_RECORDING;s_sleep_files_checked=false;assert(!power_safe(NULL));
    files[0].state=MEMO_SPOOL_PENDING;recovery_blocked=true;s_sleep_files_checked=false;assert(!power_safe(NULL));
    recovery_blocked=false;file_count=0;updated=true;run(1);
    begin_capture();key(BSP_BTN_OK,BSP_BTN_RELEASE);
    clock_us+=3000000;run(1);assert(s_sm.state==MEMO_STATE_LIST && !capture_hold && files[0].state==MEMO_SPOOL_PENDING);
    file_count=0;
    // Reboot/recovery drafts always require a decision, even with autosave on.
    files[0]=(memo_spool_entry_t){.state=MEMO_SPOOL_DRAFT,.duration_ms=3000};
    strcpy(files[0].id,"00000000-0000-4000-8000-000000000001");file_count=1;run(1);
    assert(s_sm.state==MEMO_STATE_CONFIRM && capture_hold);
    hold(BSP_BTN_OK);assert(!file_count);
    assert(memo_settings_set_autosave(false)==ESP_OK);
}

static void test_preparation_covers_resource_work(void)
{
    assert(s_sm.state==MEMO_STATE_LIST && !file_count);
    int captures=starts,attempts=prepares,cancels=cancelled_prepares;
    key(BSP_BTN_OK,BSP_BTN_PRESS);clock_us+=150000;run(1);
    assert(s_sm.state==MEMO_STATE_ARMING && s_capture_prepared && rec_prepared && !rec_active);
    assert(prepares==attempts+1 && starts==captures);
    assert(s_arm_arc->animating && s_arm_arc->anim_repeat==LV_ANIM_REPEAT_INFINITE);
    // Resources are ready early, but no sampling before the minimum hold.
    clock_us=s_arming_started+999999;run(0);assert(starts==captures);
    clock_us=s_arming_started+1000000;arm_timer_cb(NULL);run(1);
    assert(s_sm.state==MEMO_STATE_RECORDING && starts==captures+1 && prepares==attempts+1);
    key(BSP_BTN_OK,BSP_BTN_RELEASE);hold(BSP_BTN_OK);assert(!file_count);

    // Slow setup still runs behind the preparation page, never a blank screen.
    captures=starts;prepare_delay_us=1500000;
    key(BSP_BTN_OK,BSP_BTN_PRESS);clock_us+=150000;run(1);
    assert(s_sm.state==MEMO_STATE_ARMING && rec_prepared && starts==captures && s_arm_arc->animating);
    prepare_delay_us=0;run(1);assert(s_sm.state==MEMO_STATE_RECORDING && starts==captures+1);
    key(BSP_BTN_OK,BSP_BTN_RELEASE);hold(BSP_BTN_OK);

    // Release+repress while input is blocked invalidates the old hold even if
    // ADC reads OK again and the callbacks are waiting in the input queue.
    captures=starts;cancels=cancelled_prepares;repress_during_prepare=true;prepare_delay_us=1200000;
    key(BSP_BTN_OK,BSP_BTN_PRESS);clock_us+=150000;run(1);
    assert(s_sm.state==MEMO_STATE_LIST && starts==captures && cancelled_prepares==cancels+1 && !file_count);
    prepare_delay_us=0;run(2);assert(s_sm.state==MEMO_STATE_ARMING && starts==captures);
    run(3);assert(rec_prepared && starts==captures);
    key(BSP_BTN_OK,BSP_BTN_RELEASE);key(BSP_BTN_OK,BSP_BTN_CLICK);
    assert(s_sm.state==MEMO_STATE_LIST && !file_count && !rec_prepared);
}

static void test_page_resource_lifecycle(void)
{
    assert(s_sm.state==MEMO_STATE_LIST && file_count==0);
    remote_rows(2);run(1);list_select(0);
    int baseline=-1;
    for(int i=0;i<100;i++) {
        click(BSP_BTN_OK);
        assert(s_ui_page==PAGE_DETAIL && !s_list_screen && !s_record_screen && !memo_ui_settings_screen());
        hold(BSP_BTN_OK);assert(s_ui_page==PAGE_LIST && !memo_ui_detail_screen());
        hold(BSP_BTN_UP);settings_select(MEMO_SETTING_VOLUME);click(BSP_BTN_OK);click(BSP_BTN_UP);
        hold(BSP_BTN_OK);settings_select(MEMO_SETTING_NETWORK);click(BSP_BTN_OK);
        assert(s_ui_page==PAGE_PROVISION && !memo_ui_settings_screen() && !s_list_screen);
        hold(BSP_BTN_UP);assert(s_ui_page==PAGE_SETTINGS && !memo_ui_provision_screen());
        hold(BSP_BTN_UP);
        begin_capture();assert(!upload_running && !events_running && !s_list_screen && !s_arming_screen);
        key(BSP_BTN_OK,BSP_BTN_RELEASE);
        assert(s_sm.state==MEMO_STATE_CONFIRM && s_rec_workers_paused && !upload_running && !events_running);
        hold(BSP_BTN_OK);assert(upload_running && events_running && !s_record_screen && !s_rec_workers_paused);
        assert(bsp_lvgl_lock(1));memo_ui_battery_update(i%101);bsp_lvgl_unlock();
        int objects=ui_test_live_objects();
        if(baseline<0)baseline=objects;
        assert(objects==baseline); // No retained page, overlay, animation or battery registration.
    }
    for(int fault=0;fault<3;fault++) {
        int attempts=starts;
        events_stop_error=fault==0?ESP_ERR_TIMEOUT:ESP_OK;
        upload_stop_error=fault==1?ESP_ERR_TIMEOUT:ESP_OK;
        release_during_stop=fault==2;
        key(BSP_BTN_OK,BSP_BTN_PRESS);clock_us+=1000000;arm_timer_cb(NULL);run(1);
        assert(s_sm.state==MEMO_STATE_LIST && starts==attempts && !capture_hold && !s_rec_workers_paused);
        events_stop_error=upload_stop_error=0;release_during_stop=false;
        key(BSP_BTN_OK,BSP_BTN_RELEASE);key(BSP_BTN_OK,BSP_BTN_CLICK);
    }
}

int main(int argc,char **argv)
{
    ui_test_access=ui_access;
    if(argc>1){
        const bool held=!strcmp(argv[1],"wake-held");
        const bool short_wake=!strcmp(argv[1],"wake-short");
        const bool other_key=!strcmp(argv[1],"wake-up");
        assert(held || short_wake || other_key || !strcmp(argv[1],"wake-released"));
        gpio_wake=true;button_mv=(held || short_wake)?500:other_key?0:3300;remote_rows(2);app_main();run(1);
        if(held){
            assert(s_guard.armed && s_sm.state==MEMO_STATE_ARMING);
            key(BSP_BTN_OK,BSP_BTN_LONG);assert(s_guard.armed);
            clock_us+=1000000;arm_timer_cb(NULL);run(1);
            assert(s_sm.state==MEMO_STATE_RECORDING && starts==1);
            key(BSP_BTN_OK,BSP_BTN_RELEASE);key(BSP_BTN_OK,BSP_BTN_CLICK);
            assert(!s_guard.armed && s_sm.state==MEMO_STATE_CONFIRM && text_requests==0);
            hold(BSP_BTN_OK);assert(s_sm.state==MEMO_STATE_LIST && file_count==0);
        } else if(short_wake) {
            key(BSP_BTN_OK,BSP_BTN_RELEASE);key(BSP_BTN_OK,BSP_BTN_CLICK);
            assert(!s_guard.armed && s_sm.state==MEMO_STATE_LIST && starts==0 && text_requests==0);
        } else if(other_key) {
            assert(s_sm.state==MEMO_STATE_LIST && s_guard.armed);
            key(BSP_BTN_UP,BSP_BTN_LONG);key(BSP_BTN_UP,BSP_BTN_RELEASE);key(BSP_BTN_UP,BSP_BTN_CLICK);
            assert(!s_guard.armed && s_sm.state==MEMO_STATE_LIST && starts==0);
        } else assert(!s_guard.armed);
        click(BSP_BTN_DOWN);assert(s_selected==1 && s_sm.state==MEMO_STATE_LIST);
        puts("memo actual deep-wake boot guard and first fresh gesture: PASS");
        return 0;
    }
    remote_rows(12);
    deny_locks=true;app_main();deny_locks=false;run(1);
    assert(s_sm.state==MEMO_STATE_LIST && s_view_count==12);
    assert(bsp_lvgl_lock(1));assert(lv_screen_active()==s_list_screen);bsp_lvgl_unlock();
    assert(find(s_list_screen,"> 备忘"));
    assert(s_list_hint->text[0]==0);
    for(int i=0;i<LIST_ROWS;i++) {
        assert(s_row_time[i]->font==memo_font_mono() && s_row_dur[i]->font==memo_font_mono());
        assert(s_row_time[i]->w==40 && s_row_dur[i]->w==40);
        assert(s_row_time[i]->long_mode==LV_LABEL_LONG_CLIP && s_row_dur[i]->long_mode==LV_LABEL_LONG_CLIP);
        assert(s_row_text[i]->y+s_row_text[i]->h<=s_row_time[i]->y);
        assert(s_row_time[i]->x+s_row_time[i]->w+8<=s_row_dur[i]->x);
        assert(s_row_dur[i]->x+s_row_dur[i]->w<=s_rows[i]->w-12);
        assert(s_row_time[i]->y+s_row_time[i]->h<=s_rows[i]->h);
    }
    heartbeat_cadence();
    for(int i=0;i<8;i++)click(BSP_BTN_DOWN);
    assert(s_selected==8 && s_list_top==8-LIST_ROWS+1);
    test_settings_actions_survive_render_timeout();
    test_list_and_detail_page_actions();
    test_text_and_player_actions();
    test_detail_adjacent_navigation();
    test_detail_local_navigation_ownership();
    test_failed_list_page_does_not_poison_detail();
    test_capture_gesture_and_presentation();
    test_capture_draft_and_local_reader();
    test_low_memory_capture_restores_all_network_services();
    test_failed_driver_release_does_not_retry_capture();
    test_rejection_choices_and_lock_retry();
    test_provision_actions_and_wake_gesture();
    test_inspiration_capture_and_sleep();
    test_preparation_covers_resource_work();
    test_page_resource_lifecycle();
    // Every declared action is reachable from normal startup and exercised.
    for(int action=1;action<MEMO_ACT_COUNT;action++) {
        if(!actions[action])fprintf(stderr,"uncovered application action: %s\n",memo_action_name((memo_action_t)action));
        assert(actions[action]>0);
    }
    assert(bsp_lvgl_lock(1));
    memo_ui_battery_update(66);
    lv_obj_t *screens[]={s_list_screen,s_record_screen,s_arming_screen,memo_ui_detail_screen(),memo_ui_settings_screen(),memo_ui_provision_screen()};
    int live_pages=0;
    for(unsigned i=0;i<sizeof(screens)/sizeof(screens[0]);i++)if(screens[i]){
        assert(find(screens[i],"66%"));live_pages++;
    }
    assert(live_pages==1);
    memo_ui_battery_update(75);bsp_lvgl_unlock();
    puts("memo actual application input/actions/render retry integration: PASS");
}
