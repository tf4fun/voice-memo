#include <assert.h>
#include "../main/memo_events.c"

static bool done, online;
static int reads, closes, cleanups, delays, code;
SemaphoreHandle_t xSemaphoreCreateBinary(void) { return (void *)1; }
int xSemaphoreTake(SemaphoreHandle_t sem, unsigned ticks)
{ (void)sem; (void)ticks; if (!done) return 0; done = false; return pdTRUE; }
int xSemaphoreGive(SemaphoreHandle_t sem) { (void)sem; done = true; return pdTRUE; }
int xTaskCreate(void (*fn)(void *), const char *name, unsigned stack, void *arg,
                unsigned priority, TaskHandle_t *out)
{ (void)fn; (void)name; (void)stack; (void)arg; (void)priority; *out = (void *)2; return pdPASS; }
void vTaskDelete(TaskHandle_t task) { assert(task!=NULL); }
void vTaskSuspend(TaskHandle_t task) { assert(task==NULL && done); }
void vTaskDelay(unsigned ticks)
{ assert(ticks <= 100); if (++delays == 3) s_running = false; }
bool memo_net_has_ip(void) { return online; }
memo_discovery_state_t memo_discovery_state(void) { return MEMO_DISCOVERY_FOUND; }
bool memo_discovery_endpoint(memo_endpoint_t *out)
{ *out = (memo_endpoint_t){.host="127.0.0.1", .port=8080, .confirmed=true}; return true; }
void memo_discovery_invalidate(const memo_endpoint_t *endpoint) { (void)endpoint; }

static int refreshes;
void memo_api_request_refresh(void) { refreshes++; }
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *cfg)
{ assert(cfg->timeout_ms == 1000 && cfg->method == HTTP_METHOD_GET); return (void *)3; }
esp_err_t esp_http_client_set_header(esp_http_client_handle_t client, const char *name, const char *value)
{ (void)client; (void)name; (void)value; return ESP_OK; }
esp_err_t esp_http_client_open(esp_http_client_handle_t client, int len)
{ (void)client; assert(len == 0); return ESP_OK; }
int esp_http_client_fetch_headers(esp_http_client_handle_t client) { (void)client; return 0; }
int esp_http_client_get_status_code(esp_http_client_handle_t client) { (void)client; return code; }
int esp_http_client_read(esp_http_client_handle_t client, char *data, int len)
{
    (void)client; (void)data; (void)len;
    assert(memo_events_connected());
    if (++reads == 3) s_running = false;
    return -ESP_ERR_HTTP_EAGAIN;
}
esp_err_t esp_http_client_close(esp_http_client_handle_t client) { (void)client; closes++; return ESP_OK; }
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t client) { (void)client; cleanups++; return ESP_OK; }
int main(void)
{
    assert(memo_events_start() == ESP_OK);
    assert(memo_events_stop() == ESP_ERR_TIMEOUT);
    assert(s_task != NULL && !s_running);
    assert(memo_events_start() == ESP_ERR_INVALID_STATE);
    events_task(NULL); /* Completion acknowledgement arrives after timeout. */
    assert(memo_events_stop() == ESP_OK && s_task == NULL);
    assert(memo_events_start() == ESP_OK);
    code = 200;
    subscribe_once();
    assert(refreshes == 1);
    assert(reads == 3 && closes == 1 && cleanups == 1 && !memo_events_connected());
    assert(!s_running); /* Idle SSE reads honour stop, not a 120-second perform. */
    s_running = true; code = 404;
    subscribe_once();
    assert(reads == 3 && closes == 2 && cleanups == 2 && !memo_events_connected());
    online = false;
    events_task(NULL);
    assert(delays == 3 && !s_running && done);
    assert(memo_events_stop() == ESP_OK);
    puts("memo SSE stop ownership, bounded idle reads and cleanup: PASS");
}
