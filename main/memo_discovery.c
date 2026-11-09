// main/memo_discovery.c —— mDNS 客户端：找到服务端地址。
#include "memo_discovery.h"

#include <string.h>
#include <stdio.h>
#include <stdatomic.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "esp_log.h"
#include "mdns.h"

static const char *TAG = "memo_disc";

static _Atomic memo_discovery_state_t s_state = MEMO_DISCOVERY_IDLE;
static char s_host[64];
static uint16_t s_port;
static bool s_ready;
static SemaphoreHandle_t s_lock, s_address_lock;
static uint32_t s_generation;

static esp_err_t init_locked(void)
{
    if (s_ready) return ESP_OK;

    esp_err_t err = mdns_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "mdns init: %s", esp_err_to_name(err));
        return err;
    }
    s_ready = true;
    return ESP_OK;
}

static esp_err_t search_locked(uint32_t timeout_ms)
{
    if (!s_ready) {
        esp_err_t err = init_locked();
        if (err != ESP_OK) return err;
    }

    char host[64] = {0};
    uint16_t port = 0;
    xSemaphoreTake(s_address_lock, portMAX_DELAY);
    s_generation++;
    s_state = MEMO_DISCOVERY_SEARCHING;
    s_host[0] = '\0'; s_port = 0;
    xSemaphoreGive(s_address_lock);

    mdns_result_t *results = NULL;
    esp_err_t err = mdns_query_ptr(MEMO_DISCOVERY_SERVICE, MEMO_DISCOVERY_PROTO,
                                   timeout_ms, 4, &results);
    if (err != ESP_OK || results == NULL) {
        ESP_LOGW(TAG, "no %s.%s service found (%s)", MEMO_DISCOVERY_SERVICE,
                 MEMO_DISCOVERY_PROTO, esp_err_to_name(err));
        s_state = MEMO_DISCOVERY_FAILED;
        if (results != NULL) mdns_query_results_free(results);
        return ESP_ERR_NOT_FOUND;
    }

    /* PTR 结果里有时不带地址（只带回实例名），逐个看一眼再决定走哪条路。 */
    char instance[64] = {0};
    char hostname[64] = {0};
    for (mdns_result_t *r = results; r != NULL; r = r->next) {
        ESP_LOGI(TAG, "result: instance=%s host=%s port=%u addr=%s",
                 r->instance_name != NULL ? r->instance_name : "(none)",
                 r->hostname != NULL ? r->hostname : "(none)", (unsigned)r->port,
                 r->addr != NULL ? "yes" : "no");

        if (r->addr != NULL && r->addr->addr.type == ESP_IPADDR_TYPE_V4) {
            snprintf(host, sizeof(host), IPSTR, IP2STR(&r->addr->addr.u_addr.ip4));
            port = r->port;
            break;
        }
        /* TXT 里的 ip= 是最后一道保险：某些 mDNS 客户端拿不到 SRV 目标。 */
        if (host[0] == '\0' && r->txt_count > 0) {
            for (size_t i = 0; i < r->txt_count; i++) {
                if (r->txt[i].key != NULL && strcmp(r->txt[i].key, "ip") == 0 &&
                    r->txt[i].value != NULL) {
                    strncpy(host, r->txt[i].value, sizeof(host) - 1);
                    port = r->port;
                    ESP_LOGI(TAG, "address from TXT: %s:%u", host, (unsigned)port);
                    break;
                }
            }
        }
        if (instance[0] == '\0' && r->instance_name != NULL) {
            strncpy(instance, r->instance_name, sizeof(instance) - 1);
        }
        if (hostname[0] == '\0' && r->hostname != NULL) {
            strncpy(hostname, r->hostname, sizeof(hostname) - 1);
        }
        if (port == 0 && r->port != 0) port = r->port;
    }
    mdns_query_results_free(results);

    /* 兜底：用实例名查 SRV 拿主机名与端口，再查 A 记录拿 IPv4。 */
    if (host[0] == '\0' && instance[0] != '\0') {
        mdns_result_t *srv = NULL;
        if (mdns_query_srv(instance, MEMO_DISCOVERY_SERVICE, MEMO_DISCOVERY_PROTO, 2000,
                           &srv) == ESP_OK && srv != NULL) {
            if (srv->hostname != NULL) {
                strncpy(hostname, srv->hostname, sizeof(hostname) - 1);
            }
            if (srv->port != 0) port = srv->port;
            mdns_query_results_free(srv);
            ESP_LOGI(TAG, "srv: host=%s port=%u", hostname, (unsigned)port);
        }
    }
    if (host[0] == '\0' && hostname[0] != '\0' && port != 0) {
        esp_ip4_addr_t addr = {0};
        if (mdns_query_a(hostname, 2000, &addr) == ESP_OK) {
            snprintf(host, sizeof(host), IPSTR, IP2STR(&addr));
        } else {
            /* 名字解析不了就退回主机名，LWIP 会再走一次 mDNS 查询。 */
            strncpy(host, hostname, sizeof(host) - 1);
            ESP_LOGW(TAG, "no A record for %s, using hostname", hostname);
        }
    }

    if (host[0] == '\0' || port == 0) {
        ESP_LOGW(TAG, "service found but no usable address");
        s_state = MEMO_DISCOVERY_FAILED;
        return ESP_ERR_NOT_FOUND;
    }

    xSemaphoreTake(s_address_lock, portMAX_DELAY);
    snprintf(s_host, sizeof(s_host), "%s", host); s_port = port;
    s_state = MEMO_DISCOVERY_CANDIDATE;
    xSemaphoreGive(s_address_lock);
    ESP_LOGI(TAG, "server at %s:%u", host, (unsigned)port);
    return ESP_OK;
}

void memo_discovery_stop(void)
{
    if (s_lock == NULL || s_address_lock == NULL) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_ready) mdns_free();
    xSemaphoreTake(s_address_lock, portMAX_DELAY);
    s_generation++;
    s_ready = false;
    s_state = MEMO_DISCOVERY_IDLE;
    s_host[0] = '\0';
    s_port = 0;
    xSemaphoreGive(s_address_lock);
    xSemaphoreGive(s_lock);
    ESP_LOGI(TAG, "released mDNS memory");
}

memo_discovery_state_t memo_discovery_state(void)
{
    return s_state;
}

esp_err_t memo_discovery_init(void)
{
    /* First initialisation is owned by app_main before workers/input start. */
    if (s_lock == NULL) s_lock = xSemaphoreCreateMutex();
    if (s_address_lock == NULL) s_address_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL || s_address_lock == NULL) return ESP_ERR_NO_MEM;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = init_locked();
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t memo_discovery_search(uint32_t timeout_ms)
{
    if (s_lock == NULL || s_address_lock == NULL) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = search_locked(timeout_ms);
    xSemaphoreGive(s_lock);
    return err;
}

bool memo_discovery_endpoint(memo_endpoint_t *out)
{
    if (out == NULL || s_address_lock == NULL) return false;
    xSemaphoreTake(s_address_lock, portMAX_DELAY);
    bool valid = s_host[0] != '\0' && s_port != 0 &&
                 (s_state == MEMO_DISCOVERY_CANDIDATE || s_state == MEMO_DISCOVERY_FOUND);
    if (valid) {
        snprintf(out->host, sizeof(out->host), "%s", s_host);
        out->port = s_port; out->generation = s_generation;
        out->confirmed = s_state == MEMO_DISCOVERY_FOUND;
    }
    xSemaphoreGive(s_address_lock);
    return valid;
}

void memo_discovery_confirm(const memo_endpoint_t *endpoint)
{
    if (endpoint == NULL || s_address_lock == NULL) return;
    xSemaphoreTake(s_address_lock, portMAX_DELAY);
    if (endpoint->generation == s_generation && s_state == MEMO_DISCOVERY_CANDIDATE)
        s_state = MEMO_DISCOVERY_FOUND;
    xSemaphoreGive(s_address_lock);
}

void memo_discovery_invalidate(const memo_endpoint_t *endpoint)
{
    if (endpoint == NULL || s_address_lock == NULL) return;
    xSemaphoreTake(s_address_lock, portMAX_DELAY);
    if (endpoint->generation == s_generation) {
        s_generation++; s_host[0] = '\0'; s_port = 0; s_state = MEMO_DISCOVERY_FAILED;
    }
    xSemaphoreGive(s_address_lock);
}
