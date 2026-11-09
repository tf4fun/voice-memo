// Real STA module with injected driver events/configuration; no radio required.
#include <assert.h>
#include <stdio.h>
#include <stdarg.h>
static int network_snprintf(char *out,size_t cap,const char *format,...);
#undef snprintf
#define snprintf network_snprintf
#include "../main/memo_net.c"
#undef snprintf
static wifi_config_t stored;
static int connects, starts, stops, deinits, retries, inits, sntp_starts;
static esp_err_t stop_error, deinit_error, start_error;
static bool suspend_during_format, restart_during_format;
static int registrations, failed_registration;
static esp_err_t config_error;
static bool early_ip;
esp_err_t esp_event_loop_create_default(void){return ESP_OK;}
esp_err_t esp_event_handler_instance_register(esp_event_base_t base,int32_t id,
 void (*handler)(void *,esp_event_base_t,int32_t,void *),void *arg,esp_event_handler_instance_t *out)
{(void)base;(void)id;(void)handler;(void)arg;registrations++;if(registrations==failed_registration)return ESP_FAIL;*out=(void *)1;return ESP_OK;}
esp_err_t esp_netif_init(void){return ESP_OK;}
esp_netif_t *esp_netif_create_default_wifi_sta(void){static esp_netif_t n;return &n;}
esp_err_t esp_wifi_init(const wifi_init_config_t *cfg){(void)cfg;inits++;return ESP_OK;}
esp_err_t esp_wifi_get_config(int interface,wifi_config_t *out){(void)interface;*out=stored;return config_error;}
esp_err_t esp_wifi_set_mode(int mode){(void)mode;return ESP_OK;}
esp_err_t esp_wifi_set_ps(int ps){(void)ps;return ESP_OK;}
esp_err_t esp_wifi_start(void){
    starts++;
    if(early_ip){
        ip_event_got_ip_t event={.ip_info.ip.bytes={10,0,0,1}};
        wifi_event(NULL,WIFI_EVENT,WIFI_EVENT_STA_START,NULL);
        ip_event(NULL,IP_EVENT,IP_EVENT_STA_GOT_IP,&event);
    }
    return start_error;
}
esp_err_t esp_wifi_connect(void){connects++;return ESP_OK;}
esp_err_t esp_wifi_disconnect(void){return ESP_OK;}
esp_err_t esp_wifi_stop(void){stops++;return stop_error;}
esp_err_t esp_wifi_deinit(void){deinits++;return deinit_error;}
size_t esp_get_free_heap_size(void){return 100000;}
esp_err_t esp_timer_create(const esp_timer_create_args_t *args,esp_timer_handle_t *out){(void)args;*out=(void *)2;return ESP_OK;}
esp_err_t esp_timer_stop(esp_timer_handle_t timer){(void)timer;return ESP_OK;}
esp_err_t esp_timer_start_once(esp_timer_handle_t timer,uint64_t us){(void)timer;assert(us>=2000000);retries++;return ESP_OK;}
void esp_sntp_setoperatingmode(int mode){(void)mode;assert(sntp_starts==0);}
void esp_sntp_setservername(int index,const char *name){(void)index;(void)name;}
void esp_sntp_init(void){assert(sntp_starts==0);sntp_starts++;}
int esp_sntp_get_sync_status(void){return 0;} // Unreachable NTP, running but unsynced.
static int network_snprintf(char *out,size_t cap,const char *format,...)
{
    va_list args;va_start(args,format);int count=vsnprintf(out,cap,format,args);va_end(args);
    if(suspend_during_format){
        suspend_during_format=false;
        assert(memo_net_suspend()==ESP_OK);
        if(restart_during_format){early_ip=false;assert(memo_net_start()==ESP_OK);}
    }
    return count;
}

int main(void)
{
    memo_network_info_t info;
    failed_registration=2;
    assert(memo_net_start()==ESP_FAIL && memo_net_state()==MEMO_NET_FAILED);
    assert(registrations==2 && starts==0);
    failed_registration=0;
    assert(memo_net_start()==ESP_OK && memo_net_state()==MEMO_NET_NO_CREDENTIALS);
    assert(registrations==3); // retry only the missing IP handler
    assert(starts==0); memo_net_suspend(); assert(stops==0 && deinits==1);
    memcpy(stored.sta.ssid,"abcdefghijklmnopqrstuvwxyz123456",32);
    assert(memo_net_start()==ESP_OK && starts==1);
    memo_net_info(&info); assert(strlen(info.ssid)==32 && !info.ip[0]);
    wifi_event(NULL,WIFI_EVENT,WIFI_EVENT_STA_START,NULL); assert(connects==1);
    ip_event_got_ip_t event={.ip_info.ip.bytes={192,168,100,200}};
    ip_event(NULL,IP_EVENT,IP_EVENT_STA_GOT_IP,&event);
    assert(sntp_starts==1 && !s_time_synced);
    ip_event(NULL,IP_EVENT,IP_EVENT_STA_GOT_IP,&event);
    assert(sntp_starts==1); // Duplicate IP notifications cannot restart SNTP.
    assert(memo_net_has_ip()); memo_net_info(&info); assert(!strcmp(info.ip,"192.168.100.200"));
    memset(info.ssid,'x',32); memo_net_info(&info); assert(info.ssid[0]=='a');
    wifi_event(NULL,WIFI_EVENT,WIFI_EVENT_STA_DISCONNECTED,NULL);
    memo_net_info(&info); assert(!info.ip[0] && !memo_net_has_ip() && retries==1);
    memo_net_suspend(); ip_event(NULL,IP_EVENT,IP_EVENT_STA_GOT_IP,&event);
    assert(memo_net_state()==MEMO_NET_OFF); memo_net_info(&info); assert(!info.ip[0]);
    memset(&stored,0,sizeof(stored)); memcpy(stored.sta.ssid,"短网",6);
    assert(memo_net_start()==ESP_OK); memo_net_info(&info);
    assert(!strcmp(info.ssid,"短网") && !info.ip[0]);
    memo_net_suspend(); memset(&stored,0,sizeof(stored));
    assert(memo_net_start()==ESP_OK && memo_net_state()==MEMO_NET_NO_CREDENTIALS);
    memo_net_info(&info); assert(!info.ssid[0] && !info.ip[0]);
    config_error=ESP_FAIL;
    assert(memo_net_start()==ESP_FAIL && memo_net_state()==MEMO_NET_FAILED);
    config_error=0;memcpy(stored.sta.ssid,"fast",4);early_ip=true;
    assert(memo_net_start()==ESP_OK && memo_net_has_ip());
    int before=connects;
    assert(memo_net_start()==ESP_OK && connects==before && memo_net_has_ip());
    retry_cb(NULL);assert(connects==before && memo_net_has_ip());
    int before_inits=inits, before_starts=starts, before_deinits=deinits;
    stop_error=ESP_FAIL;
    assert(memo_net_suspend()==ESP_FAIL && memo_net_state()==MEMO_NET_FAILED);
    assert(s_wifi_initialized && s_release_pending && !s_wifi_stopped && deinits==before_deinits);
    memo_net_info(&info);assert(!info.ip[0]);
    ip_event(NULL,IP_EVENT,IP_EVENT_STA_GOT_IP,&event);
    assert(!memo_net_has_ip());
    assert(memo_net_start()==ESP_FAIL && inits==before_inits && starts==before_starts);
    stop_error=0;deinit_error=ESP_FAIL;
    assert(memo_net_suspend()==ESP_FAIL && s_wifi_stopped && s_wifi_initialized);
    int stopped=stops;
    assert(memo_net_suspend()==ESP_FAIL && stops==stopped); // retry deinit only
    assert(memo_net_start()==ESP_FAIL && starts==before_starts && inits==before_inits);
    deinit_error=0;
    assert(memo_net_start()==ESP_OK && memo_net_has_ip());
    assert(inits==before_inits+1 && stops==stopped && !s_release_pending);

    // SDK's already-released result is an acknowledgement, not a stuck owner.
    before_deinits=deinits;stop_error=ESP_ERR_WIFI_NOT_INIT;
    assert(memo_net_suspend()==ESP_OK && !s_wifi_initialized && deinits==before_deinits);
    stop_error=0;assert(memo_net_start()==ESP_OK);
    deinit_error=ESP_ERR_WIFI_NOT_INIT;
    assert(memo_net_suspend()==ESP_OK && !s_wifi_initialized && !s_release_pending);
    deinit_error=0;stopped=stops;before_deinits=deinits;
    assert(memo_net_suspend()==ESP_OK && stops==stopped && deinits==before_deinits);
    assert(memo_net_start()==ESP_OK);

    // Failed driver start also requires release before a new init/start.
    assert(memo_net_suspend()==ESP_OK);start_error=ESP_FAIL;
    assert(memo_net_start()==ESP_FAIL && !memo_net_has_ip() && s_release_pending);
    start_error=0;before_inits=inits;stop_error=ESP_FAIL;
    assert(memo_net_start()==ESP_FAIL && inits==before_inits);
    stop_error=0;assert(memo_net_start()==ESP_OK && memo_net_has_ip());

    // Suspend can occur after callback entry, during address formatting.
    int synced=sntp_starts;suspend_during_format=true;
    ip_event(NULL,IP_EVENT,IP_EVENT_STA_GOT_IP,&event);
    memo_net_info(&info);assert(memo_net_state()==MEMO_NET_OFF && !info.ip[0] && sntp_starts==synced);
    assert(memo_net_start()==ESP_OK);
    synced=sntp_starts;suspend_during_format=restart_during_format=true;
    ip_event(NULL,IP_EVENT,IP_EVENT_STA_GOT_IP,&event);
    memo_net_info(&info);assert(memo_net_state()==MEMO_NET_CONNECTING && !info.ip[0] && sntp_starts==synced);
    restart_during_format=false;ip_event(NULL,IP_EVENT,IP_EVENT_STA_GOT_IP,&event);
    assert(memo_net_has_ip());
    assert(sntp_starts==1 && !s_time_synced); // Includes suspend/resume/failure retries.
    puts("memo STA lifecycle/display snapshots, release faults and stale sessions: PASS");
}
