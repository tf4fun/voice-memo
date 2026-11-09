// main/memo_recorder.h —— 录音通路：I2S 输入 → Opus 编码 → spool 落盘。
//
// 编码与落盘都在 recorder 任务里完成；按钮回调只负责投递开始/停止请求。
#pragma once

#include "esp_err.h"
#include "memo_signal.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    char id[37];
    uint32_t duration_ms;
    uint32_t frame_count;
    uint32_t size_bytes;
} memo_recorder_result_t;

/* 初始化 spool 与任务握手信号（幂等）；编码器只在录音时打开。 */
esp_err_t memo_recorder_init(void);

/* 开始一条新录音。id 由调用方生成（memo_container_make_uuid）。 */
esp_err_t memo_recorder_start(const char *id);

/* Prepare stack/codec/buffers/file without sampling; same serial owner as start.
 * begin is a lightweight gate release, cancel deletes only an unstarted capture. */
esp_err_t memo_recorder_prepare(const char *id);
esp_err_t memo_recorder_begin(void);
esp_err_t memo_recorder_cancel_prepared(void);

/* 停止录音。keep=false 时删除文件。 */
esp_err_t memo_recorder_stop(bool keep, memo_recorder_result_t *result);

/* 当前电平（0..100），供录音页电平条使用；未在录音时返回 0。 */
uint8_t memo_recorder_level(void);

/* Frame-derived digital signal hint; no hardware calibration is implied. */
uint8_t memo_recorder_signal_hint(void);

/* 已经录了多久（毫秒），用于计时与 5 分钟上限。 */
uint32_t memo_recorder_elapsed_ms(void);

/* 是否正在录音。 */
bool memo_recorder_is_active(void);

/* Capture still owns resources until stop has joined and finalised them. */
bool memo_recorder_is_busy(void);
