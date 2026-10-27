// main/memo_player.h —— 本地录音回放：spool 里的 Opus 帧解码成 PCM 送到 I2S。
//
// M2 用于验证录音质量；M3 的详情页复用同一条通路（M4 会再加服务端流式播放）。
#pragma once

#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

/* 打开解码器（幂等）。 */
esp_err_t memo_player_init(void);

/* 播放 spool 里的一条录音，返回后播放仍在后台进行。 */
esp_err_t memo_player_play(const char *id);

/* 播放任意路径上的容器文件（服务端下载的临时文件用）。 */
esp_err_t memo_player_play_file(const char *path);

/* 播放服务端的一条：下载在播放任务内部完成，调用方立刻返回。 */
esp_err_t memo_player_play_remote(const char *id);

/* 下载缓存文件路径，退出详情页时清掉。 */
#define MEMO_PLAYER_CACHE_PATH "/spool/play.memo"
void memo_player_drop_cache(void);

/* 停止播放并等待任务退出。 */
esp_err_t memo_player_stop(void);

/* 是否正在播放。 */
bool memo_player_is_active(void);

/* 已播放的毫秒数（按解码出的采样数计算）。 */
uint32_t memo_player_position_ms(void);

/* 当前这条录音的总时长（来自容器头）。 */
uint32_t memo_player_duration_ms(void);
