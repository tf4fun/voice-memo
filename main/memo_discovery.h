// main/memo_discovery.h —— 通过 mDNS 找到局域网里的服务端。
//
// 服务端广播 `_voicememo._tcp`；这里查 PTR → SRV → A，得到 host:port 后交给上传模块。
#pragma once

#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

#define MEMO_DISCOVERY_SERVICE "_voicememo"
#define MEMO_DISCOVERY_PROTO "_tcp"

typedef enum {
    MEMO_DISCOVERY_IDLE = 0,
    MEMO_DISCOVERY_SEARCHING,
    MEMO_DISCOVERY_FOUND,
    MEMO_DISCOVERY_FAILED,
} memo_discovery_state_t;

/* 启动 mDNS 客户端（幂等）。 */
esp_err_t memo_discovery_init(void);

/* 查一次服务，超时 timeo_ms。成功后 memo_discovery_host/port 可用。 */
esp_err_t memo_discovery_search(uint32_t timeout_ms);

memo_discovery_state_t memo_discovery_state(void);
const char *memo_discovery_host(void);
uint16_t memo_discovery_port(void);
