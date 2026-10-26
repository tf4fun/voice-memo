// main/memo_net.c —— 常驻联网：STA 连接、重连退避、SNTP 对时。
#include "memo_net.h"

#include <string.h>
#include <time.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"

static const char *TAG = "memo_net";

/* 2020-01-01：早于它只可能是"没对过时"。 */
#define MEMO_EPOCH_FLOOR 1577836800ULL

/* 重连退避：2s 起步，翻倍到 60s 封顶。 */
#define RECONNECT_MIN_MS 2000
#define RECONNECT_MAX_MS 60000

static volatile memo_net_state_t s_state = MEMO_NET_OFF;
static char s_ip[16];
static char s_ssid[33];
static esp_netif_t *s_netif;
static esp_event_handler_instance_t s_wifi_handler;
static esp_event_handler_instance_t s_ip_handler;
static esp_timer_handle_t s_retry_timer;
static uint32_t s_backoff_ms = RECONNECT_MIN_MS;
static bool s_started;
static bool s_handlers_registered;
static volatile bool s_time_synced;

static void schedule_retry(void)
{
    if (!s_started || s_retry_timer == NULL) return;
    esp_timer_stop(s_retry_timer);
    esp_timer_start_once(s_retry_timer, (uint64_t)s_backoff_ms * 1000);
    s_backoff_ms = s_backoff_ms * 2 > RECONNECT_MAX_MS ? RECONNECT_MAX_MS : s_backoff_ms * 2;
}

static void retry_cb(void *arg)
{
    (void)arg;
    if (!s_started) return;
    s_state = MEMO_NET_CONNECTING;
    esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "connect: %s", esp_err_to_name(err));
        schedule_retry();
    }
}

static void start_sntp(void)
{
    if (s_time_synced) return;
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();
    ESP_LOGI(TAG, "SNTP started");
}

static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    if (id == WIFI_EVENT_STA_START) {
        s_state = MEMO_NET_CONNECTING;
        esp_wifi_connect();
    } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
        s_ip[0] = '\0';
        s_state = MEMO_NET_CONNECTING;
        ESP_LOGW(TAG, "disconnected, retry in %u ms", (unsigned)s_backoff_ms);
        schedule_retry();
    }
}

static void ip_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    if (id != IP_EVENT_STA_GOT_IP) return;

    const ip_event_got_ip_t *event = (const ip_event_got_ip_t *)data;
    snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&event->ip_info.ip));
    s_state = MEMO_NET_CONNECTED;
    s_backoff_ms = RECONNECT_MIN_MS;
    ESP_LOGI(TAG, "connected: ssid=%s ip=%s", s_ssid, s_ip);
    start_sntp();
}

esp_err_t memo_net_start(void)
{
    if (s_started) {
        /* 已在运行：只需重新发起连接（例如从配网交接回来）。 */
        s_state = MEMO_NET_CONNECTING;
        return esp_wifi_connect();
    }

    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;

    if (s_netif == NULL) {
        s_netif = esp_netif_create_default_wifi_sta();
        if (s_netif == NULL) return ESP_FAIL;
    }

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&init_cfg);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;

    if (!s_handlers_registered) {
        esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL,
                                            &s_wifi_handler);
        esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, ip_event, NULL,
                                            &s_ip_handler);
        s_handlers_registered = true;
    }

    if (s_retry_timer == NULL) {
        const esp_timer_create_args_t args = {.callback = retry_cb, .name = "memo_net_retry"};
        if (esp_timer_create(&args, &s_retry_timer) != ESP_OK) return ESP_ERR_NO_MEM;
    }

    /* 凭据由配网写入；这里只读取，不覆盖。 */
    wifi_config_t cfg = {0};
    esp_wifi_get_config(WIFI_IF_STA, &cfg);
    const size_t ssid_len = strnlen((const char *)cfg.sta.ssid, sizeof(cfg.sta.ssid));
    if (ssid_len == 0) {
        s_state = MEMO_NET_NO_CREDENTIALS;
        ESP_LOGI(TAG, "no stored credentials; connect via Bluetooth provisioning");
        return ESP_OK;
    }
    memcpy(s_ssid, cfg.sta.ssid, ssid_len < sizeof(s_ssid) ? ssid_len : sizeof(s_ssid) - 1);

    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) return err;
    err = esp_wifi_start();
    if (err != ESP_OK) return err;
    if (esp_wifi_set_ps(WIFI_PS_MIN_MODEM) != ESP_OK) {
        ESP_LOGW(TAG, "cannot enable modem sleep");
    }

    s_started = true;
    s_state = MEMO_NET_CONNECTING;
    s_backoff_ms = RECONNECT_MIN_MS;
    ESP_LOGI(TAG, "starting STA for %s", s_ssid);
    return ESP_OK;
}

void memo_net_suspend(void)
{
    if (!s_started) {
        s_state = MEMO_NET_OFF;
        return;
    }
    if (s_retry_timer != NULL) esp_timer_stop(s_retry_timer);
    esp_wifi_disconnect();
    esp_wifi_stop();
    esp_wifi_deinit();
    s_started = false;
    s_ip[0] = '\0';
    s_state = MEMO_NET_OFF;
    ESP_LOGI(TAG, "suspended for provisioning");
}

memo_net_state_t memo_net_state(void)
{
    return s_state;
}

const char *memo_net_ssid(void)
{
    return s_ssid;
}

const char *memo_net_ip(void)
{
    return s_netif != NULL ? s_ip : "";
}

bool memo_net_has_ip(void)
{
    return s_state == MEMO_NET_CONNECTED && s_ip[0] != '\0';
}

bool memo_net_time_synced(void)
{
    if (!s_time_synced) {
        /* 轮询一次：SNTP 同步成功后 sntp_get_sync_status 会变成 COMPLETED。 */
        if (esp_sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
            s_time_synced = true;
        }
    }
    return s_time_synced;
}

uint64_t memo_net_now(void)
{
    const time_t now = time(NULL);
    if (now > (time_t)MEMO_EPOCH_FLOOR) return (uint64_t)now;
    return 0;
}
