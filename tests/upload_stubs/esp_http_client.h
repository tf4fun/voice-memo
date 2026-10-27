#pragma once
#include "esp_err.h"
typedef void *esp_http_client_handle_t;
typedef struct {
    const char *url;
    int method;
    int timeout_ms;
    int buffer_size;
} esp_http_client_config_t;
#define HTTP_METHOD_POST 1
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *cfg);
esp_err_t esp_http_client_set_header(esp_http_client_handle_t client, const char *name, const char *value);
esp_err_t esp_http_client_open(esp_http_client_handle_t client, int len);
int esp_http_client_write(esp_http_client_handle_t client, const char *data, int len);
int esp_http_client_fetch_headers(esp_http_client_handle_t client);
int esp_http_client_get_status_code(esp_http_client_handle_t client);
esp_err_t esp_http_client_close(esp_http_client_handle_t client);
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t client);
#define HTTP_METHOD_GET 0
#define ESP_ERR_HTTP_EAGAIN 0x7007
int esp_http_client_read(esp_http_client_handle_t client, char *data, int len);
