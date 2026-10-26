// main/memo_player.h —— 本地录音回放：spool 里的 Opus 帧解码成 PCM 送到 I2S。
//
// M2 只用于验证录音质量；M3 的详情页会复用同一条通路（并加上服务端流式播放）。
#pragma once

#include "esp_err.h"

#include <stdbool.h>

/* 打开解码器（幂等）。 */
esp_err_t memo_player_init(void);

/* 播放 spool 里的一条录音，返回后播放仍在后台进行。 */
esp_err_t memo_player_play(const char *id);

/* 停止播放并等待任务退出。 */
esp_err_t memo_player_stop(void);

/* 是否正在播放。 */
bool memo_player_is_active(void);
