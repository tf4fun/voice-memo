// main/memo_discovery.c —— mDNS 客户端：找到服务端地址。
#include "memo_discovery.h"

#include <string.h>

#include "esp_log.h"
#include "mdns.h"

static const char *TAG = "memo_disc";

static volatile memo_discovery_state_t s_state = MEMO_DISCOVERY_IDLE;
static char s_host[64];
static uint16_t s_port;
static bool s_ready;

esp_err_t memo_discovery_init(void)
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

esp_err_t memo_discovery_search(uint32_t timeout_ms)
{
    if (!s_ready) {
        esp_err_t err = memo_discovery_init();
        if (err != ESP_OK) return err;
    }

    s_state = MEMO_DISCOVERY_SEARCHING;
    s_host[0] = '\0';
    s_port = 0;

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

    /* 用第一个带地址的结果。hostname 是 mDNS 名，IPv4 直接可用。 */
    for (mdns_result_t *r = results; r != NULL; r = r->next) {
        if (r->addr != NULL && r->addr->addr.type == ESP_IPADDR_TYPE_V4) {
            snprintf(s_host, sizeof(s_host), IPSTR, IP2STR(&r->addr->addr.u_addr.ip4));
            s_port = r->port;
            break;
        }
        if (r->hostname != NULL && r->port != 0) {
            strncpy(s_host, r->hostname, sizeof(s_host) - 1);
            s_port = r->port;
        }
    }
    mdns_query_results_free(results);

    if (s_host[0] == '\0' || s_port == 0) {
        ESP_LOGW(TAG, "service found but no usable address");
        s_state = MEMO_DISCOVERY_FAILED;
        return ESP_ERR_NOT_FOUND;
    }

    s_state = MEMO_DISCOVERY_FOUND;
    ESP_LOGI(TAG, "server at %s:%u", s_host, (unsigned)s_port);
    return ESP_OK;
}

memo_discovery_state_t memo_discovery_state(void)
{
    return s_state;
}

const char *memo_discovery_host(void)
{
    return s_host;
}

uint16_t memo_discovery_port(void)
{
    return s_port;
}
