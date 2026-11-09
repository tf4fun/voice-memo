#pragma once
#include <stdint.h>
#include "esp_err.h"
typedef const char *esp_event_base_t;
typedef void *esp_event_handler_instance_t;
#define WIFI_EVENT "wifi"
#define IP_EVENT "ip"
#define ESP_EVENT_ANY_ID -1
#define WIFI_EVENT_STA_START 1
#define WIFI_EVENT_STA_DISCONNECTED 2
#define IP_EVENT_STA_GOT_IP 3
esp_err_t esp_event_loop_create_default(void);
esp_err_t esp_event_handler_instance_register(esp_event_base_t base,int32_t id,
    void (*handler)(void *,esp_event_base_t,int32_t,void *),void *arg,esp_event_handler_instance_t *out);
