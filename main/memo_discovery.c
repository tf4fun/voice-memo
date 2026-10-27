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

    /* PTR 结果里有时不带地址（只带回实例名），逐个看一眼再决定走哪条路。 */
    char instance[64] = {0};
    char hostname[64] = {0};
    for (mdns_result_t *r = results; r != NULL; r = r->next) {
        ESP_LOGI(TAG, "result: instance=%s host=%s port=%u addr=%s",
                 r->instance_name != NULL ? r->instance_name : "(none)",
                 r->hostname != NULL ? r->hostname : "(none)", (unsigned)r->port,
                 r->addr != NULL ? "yes" : "no");

        if (r->addr != NULL && r->addr->addr.type == ESP_IPADDR_TYPE_V4) {
            snprintf(s_host, sizeof(s_host), IPSTR, IP2STR(&r->addr->addr.u_addr.ip4));
            s_port = r->port;
            break;
        }
        /* TXT 里的 ip= 是最后一道保险：某些 mDNS 客户端拿不到 SRV 目标。 */
        if (s_host[0] == '\0' && r->txt_count > 0) {
            for (size_t i = 0; i < r->txt_count; i++) {
                if (r->txt[i].key != NULL && strcmp(r->txt[i].key, "ip") == 0 &&
                    r->txt[i].value != NULL) {
                    strncpy(s_host, r->txt[i].value, sizeof(s_host) - 1);
                    s_port = r->port;
                    ESP_LOGI(TAG, "address from TXT: %s:%u", s_host, (unsigned)s_port);
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
        if (s_port == 0 && r->port != 0) s_port = r->port;
    }
    mdns_query_results_free(results);

    /* 兜底：用实例名查 SRV 拿主机名与端口，再查 A 记录拿 IPv4。 */
    if (s_host[0] == '\0' && instance[0] != '\0') {
        mdns_result_t *srv = NULL;
        if (mdns_query_srv(instance, MEMO_DISCOVERY_SERVICE, MEMO_DISCOVERY_PROTO, 2000,
                           &srv) == ESP_OK && srv != NULL) {
            if (srv->hostname != NULL) {
                strncpy(hostname, srv->hostname, sizeof(hostname) - 1);
            }
            if (srv->port != 0) s_port = srv->port;
            mdns_query_results_free(srv);
            ESP_LOGI(TAG, "srv: host=%s port=%u", hostname, (unsigned)s_port);
        }
    }
    if (s_host[0] == '\0' && hostname[0] != '\0' && s_port != 0) {
        esp_ip4_addr_t addr = {0};
        if (mdns_query_a(hostname, 2000, &addr) == ESP_OK) {
            snprintf(s_host, sizeof(s_host), IPSTR, IP2STR(&addr));
        } else {
            /* 名字解析不了就退回主机名，LWIP 会再走一次 mDNS 查询。 */
            strncpy(s_host, hostname, sizeof(s_host) - 1);
            ESP_LOGW(TAG, "no A record for %s, using hostname", hostname);
        }
    }

    if (s_host[0] == '\0' || s_port == 0) {
        ESP_LOGW(TAG, "service found but no usable address");
        s_state = MEMO_DISCOVERY_FAILED;
        return ESP_ERR_NOT_FOUND;
    }

    s_state = MEMO_DISCOVERY_FOUND;
    ESP_LOGI(TAG, "server at %s:%u", s_host, (unsigned)s_port);
    return ESP_OK;
}

void memo_discovery_stop(void)
{
    if (!s_ready) return;
    mdns_free();
    s_ready = false;
    s_state = MEMO_DISCOVERY_IDLE;
    s_host[0] = '\0';
    s_port = 0;
    ESP_LOGI(TAG, "released mDNS memory");
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
