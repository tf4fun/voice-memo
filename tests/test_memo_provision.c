// Execute the actual BLUFI lifecycle with SDK acknowledgements/faults injected.
#include <assert.h>
#include <stdio.h>
#include "../main/memo_provision.c"


struct provision_hs_cfg ble_hs_cfg;
static struct {bool live,token;} sems[4];
static int allocations, allocation_failure;
static int host_stops, port_deinits, profile_stops, btc_deinits, wifi_stops, wifi_deinits, unregisters, config_writes, format_errors;
static bool host_ack_late, profile_ack_late, ssid_during_get;
static esp_err_t host_stop_error, port_error, profile_error, wifi_stop_error, wifi_deinit_error, unregister_error, wifi_start_error;
static int gatt_error, gatt_init_error;
static esp_err_t get_config_error, set_config_error, enable_error, register_error;
static wifi_config_t stored;
static const esp_blufi_callbacks_t *callbacks;
static void profile_ack(void){esp_blufi_cb_param_t param={.deinit_finish.state=ESP_BLUFI_DEINIT_OK};callbacks->event_cb(ESP_BLUFI_EVENT_DEINIT_FINISH,&param);}
SemaphoreHandle_t xSemaphoreCreateBinary(void){if(++allocations==allocation_failure)return NULL;for(int i=0;i<4;i++)if(!sems[i].live){sems[i].live=true;sems[i].token=false;return &sems[i];}assert(0);return NULL;}
int xSemaphoreTake(SemaphoreHandle_t sem,unsigned ticks){(void)ticks;assert(sem);for(int i=0;i<4;i++)if(sem==&sems[i]){assert(sems[i].live);bool ready=sems[i].token;sems[i].token=false;return ready;}assert(0);return 0;}
int xSemaphoreGive(SemaphoreHandle_t sem){assert(sem);for(int i=0;i<4;i++)if(sem==&sems[i]){assert(sems[i].live);sems[i].token=true;return pdTRUE;}assert(0);return 0;}
void vSemaphoreDelete(SemaphoreHandle_t sem){for(int i=0;i<4;i++)if(sem==&sems[i]){assert(sems[i].live);sems[i].live=false;return;}assert(0);}
esp_err_t nvs_flash_init(void){return ESP_OK;}
esp_err_t esp_netif_init(void){return ESP_OK;}
esp_netif_t *esp_netif_create_default_wifi_sta(void){static esp_netif_t net;return &net;}
esp_netif_t *esp_netif_get_handle_from_ifkey(const char *key){(void)key;return esp_netif_create_default_wifi_sta();}
esp_err_t esp_event_loop_create_default(void){return ESP_OK;}
esp_err_t esp_event_handler_instance_register(esp_event_base_t base,int32_t id,void (*fn)(void *,esp_event_base_t,int32_t,void *),void *arg,esp_event_handler_instance_t *out){(void)base;(void)id;(void)fn;(void)arg;if(register_error)return register_error;*out=(void *)1;return ESP_OK;}
esp_err_t esp_event_handler_instance_unregister(esp_event_base_t base,int32_t id,esp_event_handler_instance_t handler){(void)base;(void)id;(void)handler;unregisters++;return unregister_error;}
esp_err_t esp_wifi_init(const wifi_init_config_t *cfg){(void)cfg;return ESP_OK;}
esp_err_t esp_wifi_set_storage(int storage){(void)storage;return ESP_OK;}
esp_err_t esp_wifi_get_config(int iface,wifi_config_t *cfg){
    (void)iface;*cfg=stored;
    if(ssid_during_get){
        ssid_during_get=false;
        esp_blufi_cb_param_t param={.sta_ssid={.ssid=(uint8_t *)"new",.ssid_len=3}};
        callbacks->event_cb(ESP_BLUFI_EVENT_RECV_STA_SSID,&param);
    }
    return get_config_error;
}
esp_err_t esp_wifi_set_config(int iface,const wifi_config_t *cfg){(void)iface;config_writes++;if(set_config_error)return set_config_error;stored=*cfg;return ESP_OK;}
esp_err_t esp_wifi_get_mode(wifi_mode_t *mode){*mode=WIFI_MODE_STA;return ESP_OK;}
esp_err_t esp_wifi_set_mode(wifi_mode_t mode){(void)mode;return ESP_OK;}
esp_err_t esp_wifi_start(void){return wifi_start_error;}
esp_err_t esp_wifi_stop(void){wifi_stops++;return wifi_stop_error;}
esp_err_t esp_wifi_deinit(void){wifi_deinits++;return wifi_deinit_error;}
esp_err_t esp_wifi_connect(void){return ESP_OK;}
esp_err_t esp_wifi_disconnect(void){return ESP_OK;}
esp_err_t esp_wifi_scan_start(const wifi_scan_config_t *cfg,bool block){(void)cfg;(void)block;return ESP_OK;}
esp_err_t esp_wifi_scan_stop(void){return ESP_OK;}
esp_err_t esp_wifi_scan_get_ap_records(uint16_t *count,wifi_ap_record_t *records){(void)records;*count=0;return ESP_OK;}
size_t esp_get_free_heap_size(void){return 100000;}
size_t heap_caps_get_largest_free_block(unsigned caps){(void)caps;return 80000;}
esp_err_t esp_blufi_register_callbacks(const esp_blufi_callbacks_t *cb){callbacks=cb;return ESP_OK;}
esp_err_t esp_blufi_profile_init(void){esp_blufi_cb_param_t param={0};callbacks->event_cb(ESP_BLUFI_EVENT_INIT_FINISH,&param);return ESP_OK;}
esp_err_t esp_blufi_profile_deinit(void){profile_stops++;if(!profile_error && !profile_ack_late)profile_ack();return profile_error;}
int esp_blufi_gatt_svr_init(void){return gatt_init_error;}
int esp_blufi_gatt_svr_deinit(void){assert(!s_host_running);return gatt_error;}
void esp_blufi_gatt_svr_register_cb(void){}
void esp_blufi_btc_init(void){}
void esp_blufi_btc_deinit(void){assert(!s_profile_initialized && !s_host_running && !s_port_initialized);btc_deinits++;}
void esp_blufi_adv_start_with_name(const char *name){assert(!strcmp(name,MEMO_PROV_DEVICE_NAME));}
void esp_blufi_adv_stop(void){}
void esp_blufi_disconnect(void){}
void esp_blufi_send_error_info(int error){if(error==ESP_BLUFI_DATA_FORMAT_ERROR)format_errors++;}
void esp_blufi_send_wifi_conn_report(wifi_mode_t mode,esp_blufi_sta_conn_state_t state,int count,const esp_blufi_extra_info_t *info){(void)mode;(void)state;(void)count;assert(info->sta_ssid_len<=32);}
void esp_blufi_send_wifi_list(uint16_t count,const esp_blufi_ap_record_t *records){(void)count;(void)records;}
esp_err_t nimble_port_init(void){return ESP_OK;}
esp_err_t nimble_port_deinit(void){assert(!s_host_running && !s_gatt_initialized);port_deinits++;return port_error;}
int nimble_port_stop(void){host_stops++;if(!host_stop_error && !host_ack_late)xSemaphoreGive(s_host_stopped);return host_stop_error;}
void nimble_port_run(void){}
void nimble_port_freertos_deinit(void){}
esp_err_t esp_nimble_enable(void (*task)(void *)){(void)task;if(enable_error)return enable_error;ble_hs_cfg.sync_cb();return ESP_OK;}
int ble_svc_gap_device_name_set(const char *name){(void)name;return 0;}
void memo_provision_security_negotiate(uint8_t *d,int n,uint8_t **o,int *l,bool *f){(void)d;(void)n;(void)o;(void)l;(void)f;}
int memo_provision_security_encrypt(uint8_t v,uint8_t *d,int n){(void)v;(void)d;return n;}
int memo_provision_security_decrypt(uint8_t v,uint8_t *d,int n){(void)v;(void)d;return n;}
uint16_t memo_provision_security_checksum(uint8_t v,uint8_t *d,int n){(void)v;(void)d;(void)n;return 0;}
int memo_provision_security_init(void){return 0;}
void memo_provision_security_deinit(void){}
static void clean(void){assert(!s_host_initialized && !s_wifi_initialized && !s_ip_handler_registered && !s_wifi_handler_registered);assert(!s_host_stopped && !s_profile_stopped);for(int i=0;i<4;i++)assert(!sems[i].live);}
static void start(void){assert(memo_provision_start()==ESP_OK);assert(s_host_running && s_profile_initialized && memo_provision_state()==MEMO_PROV_ADVERTISING);}
static void wifi(void){assert(ensure_wifi()==ESP_OK);assert(s_wifi_initialized && s_wifi_started && s_wifi_stop_needed);}
int main(void)
{
    start();wifi();
    host_ack_late=true;
    assert(memo_provision_stop()==ESP_ERR_TIMEOUT && s_host_running && s_host_initialized && !s_accept_callbacks);
    int stopped=host_stops;assert(memo_provision_start()==ESP_ERR_INVALID_STATE);
    assert(memo_provision_stop()==ESP_ERR_TIMEOUT && host_stops==stopped);
    host_ack_late=false;xSemaphoreGive(s_host_stopped);profile_ack_late=true;
    int freed=btc_deinits;
    assert(memo_provision_stop()==ESP_ERR_TIMEOUT && !s_host_running && s_profile_initialized && btc_deinits==freed);
    stopped=profile_stops;
    assert(memo_provision_stop()==ESP_ERR_TIMEOUT && profile_stops==stopped);
    profile_ack_late=false;profile_ack();
    assert(memo_provision_stop()==ESP_OK);clean();

    start();wifi();wifi_stop_error=ESP_FAIL;freed=wifi_deinits;
    assert(memo_provision_stop()==ESP_FAIL && s_wifi_started && s_wifi_initialized && wifi_deinits==freed);
    assert(memo_provision_start()==ESP_ERR_INVALID_STATE);
    wifi_stop_error=0;wifi_deinit_error=ESP_FAIL;
    assert(memo_provision_stop()==ESP_FAIL && !s_wifi_started && s_wifi_initialized);
    stopped=wifi_stops;
    assert(memo_provision_stop()==ESP_FAIL && wifi_stops==stopped);
    wifi_deinit_error=0;assert(memo_provision_stop()==ESP_OK);clean();

    start();wifi();unregister_error=ESP_FAIL;
    assert(memo_provision_stop()==ESP_FAIL && s_ip_handler_registered && s_wifi_initialized);
    assert(memo_provision_start()==ESP_ERR_INVALID_STATE);
    unregister_error=0;assert(memo_provision_stop()==ESP_OK);clean();

    start();port_error=ESP_FAIL;
    assert(memo_provision_stop()==ESP_FAIL && s_host_initialized && s_port_initialized);
    port_error=0;profile_error=ESP_FAIL;
    assert(memo_provision_stop()==ESP_FAIL && !s_port_initialized && s_profile_initialized);
    int ports=port_deinits;profile_error=0;assert(memo_provision_stop()==ESP_OK && port_deinits==ports);clean();

    start();host_stop_error=ESP_FAIL;
    assert(memo_provision_stop()==ESP_FAIL && s_host_running);
    host_stop_error=0;gatt_error=1;
    assert(memo_provision_stop()==ESP_FAIL && !s_host_running && s_gatt_initialized);
    gatt_error=0;assert(memo_provision_stop()==ESP_OK);clean();

    allocation_failure=allocations+2;
    assert(memo_provision_start()==ESP_ERR_NO_MEM);clean();allocation_failure=0;
    start();wifi_start_error=ESP_FAIL;
    assert(ensure_wifi()==ESP_FAIL && s_wifi_initialized && s_wifi_stop_needed);
    wifi_start_error=0;assert(memo_provision_stop()==ESP_OK);clean();

    start();wifi();wifi_stop_error=ESP_ERR_WIFI_NOT_INIT;
    assert(memo_provision_stop()==ESP_OK);clean();wifi_stop_error=0;
    start();wifi();wifi_deinit_error=ESP_ERR_WIFI_NOT_INIT;
    assert(memo_provision_stop()==ESP_OK);clean();wifi_deinit_error=0;
    start();wifi();
    esp_blufi_cb_param_t param={.sta_ssid={.ssid=(uint8_t *)"abcdefghijklmnopqrstuvwxyz123456",.ssid_len=32}};
    callbacks->event_cb(ESP_BLUFI_EVENT_RECV_STA_SSID,&param);
    memo_network_info_t info;memo_provision_info(&info);assert(strlen(info.ssid)==32);
    int writes=config_writes, invalid=format_errors;
    param.sta_ssid.ssid_len=-1;callbacks->event_cb(ESP_BLUFI_EVENT_RECV_STA_SSID,&param);
    param.sta_ssid.ssid_len=33;callbacks->event_cb(ESP_BLUFI_EVENT_RECV_STA_SSID,&param);
    param.sta_ssid.ssid_len=1;param.sta_ssid.ssid=NULL;callbacks->event_cb(ESP_BLUFI_EVENT_RECV_STA_SSID,&param);
    param=(esp_blufi_cb_param_t){.sta_passwd={.passwd=(uint8_t *)"invalid-test",.passwd_len=-1}};
    callbacks->event_cb(ESP_BLUFI_EVENT_RECV_STA_PASSWD,&param);
    assert(config_writes==writes && format_errors==invalid+4);
    ssid_during_get=true;wifi_event(NULL,WIFI_EVENT,WIFI_EVENT_STA_START,NULL);
    memo_provision_info(&info);assert(!strcmp(info.ssid,"new") && !strcmp((char *)stored.sta.ssid,"new"));
    set_config_error=ESP_FAIL;param=(esp_blufi_cb_param_t){.sta_ssid={.ssid=(uint8_t *)"rejected",.ssid_len=8}};
    callbacks->event_cb(ESP_BLUFI_EVENT_RECV_STA_SSID,&param);
    memo_provision_info(&info);assert(memo_provision_state()==MEMO_PROV_FAILED && !strcmp(info.ssid,"new"));
    set_config_error=0;get_config_error=ESP_FAIL;wifi_event(NULL,WIFI_EVENT,WIFI_EVENT_STA_START,NULL);
    assert(memo_provision_state()==MEMO_PROV_FAILED);get_config_error=0;
    assert(memo_provision_stop()==ESP_OK);clean();

    gatt_init_error=1;assert(memo_provision_start()==ESP_FAIL);clean();gatt_init_error=0;
    enable_error=ESP_ERR_NO_MEM;assert(memo_provision_start()==ESP_ERR_NO_MEM);clean();enable_error=0;
    start();register_error=ESP_FAIL;assert(ensure_wifi()==ESP_FAIL && s_wifi_initialized);
    register_error=0;assert(memo_provision_stop()==ESP_OK);clean();
    puts("memo actual BLUFI ownership, acknowledgements and driver release faults: PASS");
}
