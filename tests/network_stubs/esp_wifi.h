#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
typedef struct { struct { uint8_t ssid[32]; } sta; } wifi_config_t;
typedef struct { int unused; } wifi_init_config_t;
#define WIFI_INIT_CONFIG_DEFAULT() ((wifi_init_config_t){0})
#define ESP_ERR_WIFI_NOT_INIT 0x3001
#define WIFI_IF_STA 0
#define WIFI_MODE_STA 0
#define WIFI_PS_MIN_MODEM 0
esp_err_t esp_wifi_init(const wifi_init_config_t *cfg);
esp_err_t esp_wifi_get_config(int interface,wifi_config_t *out);
esp_err_t esp_wifi_set_mode(int mode);
esp_err_t esp_wifi_set_ps(int ps);
esp_err_t esp_wifi_start(void);
esp_err_t esp_wifi_connect(void);
esp_err_t esp_wifi_disconnect(void);
esp_err_t esp_wifi_stop(void);
esp_err_t esp_wifi_deinit(void);
size_t esp_get_free_heap_size(void);
