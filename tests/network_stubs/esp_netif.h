#pragma once
#include <stdint.h>
#include "esp_err.h"
typedef struct { int unused; } esp_netif_t;
typedef struct { struct { unsigned char bytes[4]; } ip; } test_ip_info_t;
typedef struct { test_ip_info_t ip_info; } ip_event_got_ip_t;
#define IPSTR "%u.%u.%u.%u"
#define IP2STR(ip) (ip)->bytes[0],(ip)->bytes[1],(ip)->bytes[2],(ip)->bytes[3]
esp_err_t esp_netif_init(void);
esp_netif_t *esp_netif_create_default_wifi_sta(void);
