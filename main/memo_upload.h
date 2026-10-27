// main/memo_upload.h —— 把唯一一条待上传录音传给服务端。
//
// Accepted uploads release the spool. Rejected recordings wait for a user
// retry/delete choice; every unresolved file blocks a new capture.
#pragma once

#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>
#include "memo_upload_policy.h"

typedef enum {
    MEMO_UPLOAD_IDLE = 0,
    MEMO_UPLOAD_DISCOVERING,
    MEMO_UPLOAD_SENDING,
    MEMO_UPLOAD_DONE,
    MEMO_UPLOAD_FAILED,
    MEMO_UPLOAD_REVIEW,
} memo_upload_state_t;

/* 启动上传任务（幂等）：有网络就发现服务端并上传，没有就等。 */
esp_err_t memo_upload_start(void);

/* 停止任务（用于配网等独占场景）。 */
/* Timeout retains ownership; call again to join before releasing network. */
esp_err_t memo_upload_stop(void);

memo_upload_state_t memo_upload_state(void);

/* 是否还有一条录音占着"处理中"这个状态（此时应阻止新录音）。 */
bool memo_upload_pending(void);

/* 最近一次失败原因，供界面显示。 */
const char *memo_upload_error(void);

typedef struct {
    char id[37];
    uint64_t recorded_at;
    uint32_t duration_ms;
    bool legacy_kept; /* Compatibility with files from the superseded build. */
    bool deciding;
    bool operation_failed;
} memo_upload_rejection_t;

/* Snapshot copied under a mutex. A .bad file survives reboot until chosen. */
bool memo_upload_rejection(memo_upload_rejection_t *out);

/* Queue retry/delete for the worker. Retry resets the same ID to a fresh
 * three-attempt round; only an explicit delete removes a rejected file. */
esp_err_t memo_upload_choose(const char *id, bool delete_recording);

/* Keep the upload worker away from an active/unconfirmed capture. */
void memo_upload_set_capture_busy(bool busy);
