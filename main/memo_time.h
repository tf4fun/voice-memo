// main/memo_time.h —— 时间与时长格式化的纯逻辑部分。
//
// 存的是 UTC 秒；显示用设置里的时区偏移。分组（今天/昨天/更早）也在这里判定，
// 免得 UI 里散落一堆边界判断。
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    MEMO_DAY_TODAY = 0,
    MEMO_DAY_YESTERDAY,
    MEMO_DAY_EARLIER,
} memo_day_t;

/* 把 UTC 秒按分钟级时区偏移换算成「自 1970 起的本地秒」。 */
int64_t memo_time_local(int64_t utc_seconds, int32_t tz_offset_min);

/* 相对 now 判断落在哪一天。两者都传 UTC 秒，按同一偏移换算后比较。 */
memo_day_t memo_time_classify(int64_t now_utc, int64_t ts_utc, int32_t tz_offset_min);

/* 写 "08:32"，容量不足时写空串并返回 false。 */
bool memo_time_clock(int64_t utc_seconds, int32_t tz_offset_min, char *dst, size_t cap);

/* 写日期，例如 "10-24"。 */
bool memo_time_date(int64_t utc_seconds, int32_t tz_offset_min, char *dst, size_t cap);

/* 写时长："0:09"、"1:24"、"12:05"。负数按 0 处理。 */
bool memo_time_duration(int64_t milliseconds, char *dst, size_t cap);

/* 录音计时用的紧凑格式："01:23"，超过一小时为 "1:02:03"。 */
bool memo_time_timer(int64_t milliseconds, char *dst, size_t cap);
