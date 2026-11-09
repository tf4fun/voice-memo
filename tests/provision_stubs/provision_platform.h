#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <pthread.h>
#include "esp_err.h"
typedef pthread_mutex_t portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED PTHREAD_MUTEX_INITIALIZER
#define portENTER_CRITICAL(p) ((void)pthread_mutex_lock(p))
#define portEXIT_CRITICAL(p) ((void)pthread_mutex_unlock(p))
typedef void *SemaphoreHandle_t;
#define pdTRUE 1
#define pdMS_TO_TICKS(ms) (ms)
SemaphoreHandle_t xSemaphoreCreateBinary(void);
int xSemaphoreTake(SemaphoreHandle_t sem,unsigned ticks);
int xSemaphoreGive(SemaphoreHandle_t sem);
void vSemaphoreDelete(SemaphoreHandle_t sem);
typedef const char *esp_event_base_t;
typedef void *esp_event_handler_instance_t;
#define WIFI_EVENT "wifi"
#define IP_EVENT "ip"
#define ESP_EVENT_ANY_ID -1
#define WIFI_EVENT_STA_START 1
#define WIFI_EVENT_STA_DISCONNECTED 2
#define WIFI_EVENT_SCAN_DONE 3
#define IP_EVENT_STA_GOT_IP 4
#define ESP_ERR_WIFI_NOT_INIT 0x3001
#define WIFI_IF_STA 0
#define WIFI_MODE_STA 1
#define WIFI_AUTH_OPEN 0
#define WIFI_STORAGE_FLASH 1
typedef int wifi_mode_t;
typedef struct {struct {uint8_t ssid[32],password[64],bssid[6];bool bssid_set;struct {int authmode;} threshold;} sta;} wifi_config_t;
typedef struct {int unused;} wifi_init_config_t;
#define WIFI_INIT_CONFIG_DEFAULT() ((wifi_init_config_t){0})
typedef struct {int unused;} wifi_scan_config_t;
typedef struct {uint8_t ssid[33];int rssi;} wifi_ap_record_t;
typedef struct {uint8_t reason;} wifi_event_sta_disconnected_t;
typedef struct {int unused;} esp_netif_t;
typedef struct {struct {struct {unsigned char bytes[4];} ip;} ip_info;} ip_event_got_ip_t;
#define IPSTR "%u.%u.%u.%u"
#define IP2STR(ip) (ip)->bytes[0],(ip)->bytes[1],(ip)->bytes[2],(ip)->bytes[3]
esp_err_t nvs_flash_init(void);
esp_err_t esp_netif_init(void);
esp_netif_t *esp_netif_create_default_wifi_sta(void);
esp_netif_t *esp_netif_get_handle_from_ifkey(const char *key);
esp_err_t esp_event_loop_create_default(void);
esp_err_t esp_event_handler_instance_register(esp_event_base_t base,int32_t id,void (*fn)(void *,esp_event_base_t,int32_t,void *),void *arg,esp_event_handler_instance_t *out);
esp_err_t esp_event_handler_instance_unregister(esp_event_base_t base,int32_t id,esp_event_handler_instance_t handler);
esp_err_t esp_wifi_init(const wifi_init_config_t *cfg);
esp_err_t esp_wifi_set_storage(int storage);
esp_err_t esp_wifi_get_config(int iface,wifi_config_t *cfg);
esp_err_t esp_wifi_set_config(int iface,const wifi_config_t *cfg);
esp_err_t esp_wifi_get_mode(wifi_mode_t *mode);
esp_err_t esp_wifi_set_mode(wifi_mode_t mode);
esp_err_t esp_wifi_start(void);
esp_err_t esp_wifi_stop(void);
esp_err_t esp_wifi_deinit(void);
esp_err_t esp_wifi_connect(void);
esp_err_t esp_wifi_disconnect(void);
esp_err_t esp_wifi_scan_start(const wifi_scan_config_t *cfg,bool block);
esp_err_t esp_wifi_scan_stop(void);
esp_err_t esp_wifi_scan_get_ap_records(uint16_t *count,wifi_ap_record_t *records);
#define MALLOC_CAP_8BIT 1
size_t esp_get_free_heap_size(void);
size_t heap_caps_get_largest_free_block(unsigned caps);
typedef enum {
 ESP_BLUFI_EVENT_INIT_FINISH,ESP_BLUFI_EVENT_DEINIT_FINISH,ESP_BLUFI_EVENT_BLE_CONNECT,
 ESP_BLUFI_EVENT_BLE_DISCONNECT,ESP_BLUFI_EVENT_SET_WIFI_OPMODE,ESP_BLUFI_EVENT_RECV_STA_BSSID,
 ESP_BLUFI_EVENT_RECV_STA_SSID,ESP_BLUFI_EVENT_RECV_STA_PASSWD,ESP_BLUFI_EVENT_REQ_CONNECT_TO_AP,
 ESP_BLUFI_EVENT_REQ_DISCONNECT_FROM_AP,ESP_BLUFI_EVENT_GET_WIFI_STATUS,ESP_BLUFI_EVENT_GET_WIFI_LIST,
 ESP_BLUFI_EVENT_RECV_SLAVE_DISCONNECT_BLE,ESP_BLUFI_EVENT_DEAUTHENTICATE_STA,ESP_BLUFI_EVENT_REPORT_ERROR
} esp_blufi_cb_event_t;
typedef struct {
 struct {int state;} deinit_finish;
 struct {uint8_t bssid[6];} sta_bssid;
 struct {uint8_t *ssid;int ssid_len;} sta_ssid;
 struct {uint8_t *passwd;int passwd_len;} sta_passwd;
 struct {int state;} report_error;
} esp_blufi_cb_param_t;
typedef struct {
 void (*event_cb)(esp_blufi_cb_event_t,esp_blufi_cb_param_t *);
 void (*negotiate_data_handler)(uint8_t *,int,uint8_t **,int *,bool *);
 int (*encrypt_func)(uint8_t,uint8_t *,int),(*decrypt_func)(uint8_t,uint8_t *,int);
 uint16_t (*checksum_func)(uint8_t,uint8_t *,int);
} esp_blufi_callbacks_t;
typedef int esp_blufi_sta_conn_state_t;
#define ESP_BLUFI_STA_CONN_SUCCESS 0
#define ESP_BLUFI_STA_CONNECTING 1
#define ESP_BLUFI_STA_CONN_FAIL 2
#define ESP_BLUFI_WIFI_SCAN_FAIL 3
#define ESP_BLUFI_DATA_FORMAT_ERROR 4
#define ESP_BLUFI_DEINIT_OK 0
typedef struct {uint8_t *sta_ssid;size_t sta_ssid_len;} esp_blufi_extra_info_t;
typedef struct {uint8_t ssid[33];int rssi;} esp_blufi_ap_record_t;
esp_err_t esp_blufi_register_callbacks(const esp_blufi_callbacks_t *callbacks);
esp_err_t esp_blufi_profile_init(void);
esp_err_t esp_blufi_profile_deinit(void);
int esp_blufi_gatt_svr_init(void);
int esp_blufi_gatt_svr_deinit(void);
void esp_blufi_gatt_svr_register_cb(void);
void esp_blufi_btc_init(void);
void esp_blufi_btc_deinit(void);
void esp_blufi_adv_start_with_name(const char *name);
void esp_blufi_adv_stop(void);
void esp_blufi_disconnect(void);
void esp_blufi_send_error_info(int error);
void esp_blufi_send_wifi_conn_report(wifi_mode_t mode,esp_blufi_sta_conn_state_t state,int count,const esp_blufi_extra_info_t *info);
void esp_blufi_send_wifi_list(uint16_t count,const esp_blufi_ap_record_t *records);
struct provision_hs_cfg {void (*reset_cb)(int);void (*sync_cb)(void);void (*gatts_register_cb)(void);};
extern struct provision_hs_cfg ble_hs_cfg;
esp_err_t nimble_port_init(void);
esp_err_t nimble_port_deinit(void);
int nimble_port_stop(void);
void nimble_port_run(void);
void nimble_port_freertos_deinit(void);
esp_err_t esp_nimble_enable(void (*task)(void *));
int ble_svc_gap_device_name_set(const char *name);
