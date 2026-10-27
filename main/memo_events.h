// main/memo_events.h —— 订阅服务端的 SSE 事件流。
//
// 只在网络可用时连接；转写完成这类变化会立刻到达，不必轮询。
// 断线后自动重连，重连期间界面仍可用（低频轮询作为兜底）。
#pragma once

#include "esp_err.h"

#include <stdbool.h>

/* 建常驻订阅任务（幂等）。 */
esp_err_t memo_events_start(void);

/* 断开并结束任务（配网/录音让位时调用）。 */
void memo_events_stop(void);

/* 当前是否处于已连接状态。 */
bool memo_events_connected(void);
