// main/memo_provision.h —— BLUFI 配网入口与状态查询。
//
// 状态查询供界面轮询：设置页把"网络"行切到配网页后，每 100 ms 读一次即可，
// 不需要回调。
#pragma once

#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    MEMO_PROV_IDLE = 0,
    MEMO_PROV_STARTING,
    MEMO_PROV_ADVERTISING,
    MEMO_PROV_BLE_CONNECTED,
    MEMO_PROV_WIFI_CONNECTING,
    MEMO_PROV_WIFI_CONNECTED,
    MEMO_PROV_FAILED,
} memo_provision_state_t;

/* 广播名必须以 BLUFI 开头，微信小程序才列得出来。 */
#define MEMO_PROV_DEVICE_NAME "BLUFI_MEMO"

/* 启动 Wi-Fi + BLUFI；失败会自动回滚并把状态置为 FAILED。 */
esp_err_t memo_provision_start(void);

/* 停止并释放 BLE / Wi-Fi 资源（设置页退出时调用）。 */
void memo_provision_stop(void);

memo_provision_state_t memo_provision_state(void);

/* 已连接（或正在连接）的 SSID；没有则返回空串。 */
const char *memo_provision_ssid(void);

/* 拿到的 IPv4 地址；没有则返回空串。 */
const char *memo_provision_ip(void);

/* 最近一次失败的错误码。 */
esp_err_t memo_provision_error(void);

/* 是否已经拿到 IP（后续联网功能据此判断）。 */
bool memo_provision_has_network(void);
