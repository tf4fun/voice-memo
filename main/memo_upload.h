// main/memo_upload.h —— 把唯一一条待上传录音传给服务端。
//
// 局部存储是"处理中"状态：上传成功即删除，失败按退避重试；在它消失之前不允许开始
// 新的录音（产品决定：阻塞优于排队）。
#pragma once

#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    MEMO_UPLOAD_IDLE = 0,
    MEMO_UPLOAD_DISCOVERING,
    MEMO_UPLOAD_SENDING,
    MEMO_UPLOAD_DONE,
    MEMO_UPLOAD_FAILED,
} memo_upload_state_t;

/* 启动上传任务（幂等）：有网络就发现服务端并上传，没有就等。 */
esp_err_t memo_upload_start(void);

/* 停止任务（用于配网等独占场景）。 */
void memo_upload_stop(void);

memo_upload_state_t memo_upload_state(void);

/* 是否还有一条录音占着"处理中"这个状态（此时应阻止新录音）。 */
bool memo_upload_pending(void);

/* 最近一次失败原因，供界面显示。 */
const char *memo_upload_error(void);
