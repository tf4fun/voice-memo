// main/memo_recorder.h —— 录音通路：I2S 输入 → Opus 编码 → spool 落盘。
//
// 编码与落盘都在 recorder 任务里完成；按钮回调只负责投递开始/停止请求。
#pragma once

#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    char id[37];
    uint32_t duration_ms;
    uint32_t frame_count;
    uint32_t size_bytes;
} memo_recorder_result_t;

/* 初始化编码器与 spool（幂等）。失败会回滚本次创建的资源。 */
esp_err_t memo_recorder_init(void);

/* 开始一条新录音。id 由调用方生成（memo_container_make_uuid）。 */
esp_err_t memo_recorder_start(const char *id);

/* 停止录音。keep=false 时删除文件。 */
esp_err_t memo_recorder_stop(bool keep, memo_recorder_result_t *result);

/* 当前电平（0..100），供录音页电平条使用；未在录音时返回 0。 */
uint8_t memo_recorder_level(void);

/* 已经录了多久（毫秒），用于计时与 5 分钟上限。 */
uint32_t memo_recorder_elapsed_ms(void);

/* 是否正在录音。 */
bool memo_recorder_is_active(void);
