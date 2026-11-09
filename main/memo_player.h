// Local MEMO playback and remote HTTP streaming, decoded to PCM/I2S.
#pragma once

#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

/* Initialise the join semaphore; decoder opens per playback. */
esp_err_t memo_player_init(void);

/* Play a local spool container asynchronously. */
esp_err_t memo_player_play_file(const char *path);

/* Stream from the server asynchronously, without a Flash download cache. */
esp_err_t memo_player_play_remote(const char *id);

/* 停止播放并等待任务退出。 */
esp_err_t memo_player_stop(void);

/* Pause at a frame boundary, retaining decoder, source and playback position.
 * Resume continues that source. Requests are nonblocking; current I2S output
 * may finish. A disconnected remote source ends normally rather than replaying. */
esp_err_t memo_player_pause(void);
esp_err_t memo_player_resume(void);
bool memo_player_is_paused(void);

/* Task still owns playback resources, including stop timeout/completion awaiting join. */
bool memo_player_is_busy(void);
/* Nonblocking join of a naturally completed worker; application owner only. */
void memo_player_poll(void);

/* 是否正在播放。 */
bool memo_player_is_active(void);

/* 已播放的毫秒数；结束时保留最后进度，下一次播放请求重置。 */
uint32_t memo_player_position_ms(void);

/* 当前这条录音的总时长（来自容器头）。 */
uint32_t memo_player_duration_ms(void);
