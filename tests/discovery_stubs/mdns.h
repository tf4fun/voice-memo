#pragma once
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#define ESP_IPADDR_TYPE_V4 0
#define IPSTR "%u.%u.%u.%u"
#define IP2STR(p) ((unsigned)((p)->addr & 255)), ((unsigned)(((p)->addr>>8)&255)), ((unsigned)(((p)->addr>>16)&255)), ((unsigned)(((p)->addr>>24)&255))
typedef struct { uint32_t addr; } esp_ip4_addr_t;
typedef struct { int type; union { esp_ip4_addr_t ip4; } u_addr; } esp_ip_addr_t;
typedef struct { esp_ip_addr_t addr; } mdns_ip_addr_t;
typedef struct { const char *key, *value; } mdns_txt_item_t;
typedef struct mdns_result { struct mdns_result *next; const char *instance_name,*hostname; uint16_t port; mdns_ip_addr_t *addr; size_t txt_count; mdns_txt_item_t *txt; } mdns_result_t;
esp_err_t mdns_init(void);
void mdns_free(void);
esp_err_t mdns_query_ptr(const char *service,const char *proto,uint32_t timeout,size_t max,mdns_result_t **out);
esp_err_t mdns_query_srv(const char *instance,const char *service,const char *proto,uint32_t timeout,mdns_result_t **out);
esp_err_t mdns_query_a(const char *hostname,uint32_t timeout,esp_ip4_addr_t *out);
void mdns_query_results_free(mdns_result_t *results);
