// main/memo_time.c —— 纯时间工具，不依赖 ESP-IDF。
#include "memo_time.h"

#include <stdio.h>

#define SECONDS_PER_DAY 86400LL

int64_t memo_time_local(int64_t utc_seconds, int32_t tz_offset_min)
{
    return utc_seconds + (int64_t)tz_offset_min * 60;
}

/* 向下取整的整数除法，负时间也能落到正确的那一天。 */
static int64_t floor_div(int64_t value, int64_t divisor)
{
    int64_t quotient = value / divisor;
    if ((value % divisor != 0) && ((value < 0) != (divisor < 0))) {
        quotient--;
    }
    return quotient;
}

memo_day_t memo_time_classify(int64_t now_utc, int64_t ts_utc, int32_t tz_offset_min)
{
    int64_t today = floor_div(memo_time_local(now_utc, tz_offset_min), SECONDS_PER_DAY);
    int64_t that = floor_div(memo_time_local(ts_utc, tz_offset_min), SECONDS_PER_DAY);

    if (that == today) return MEMO_DAY_TODAY;
    if (that == today - 1) return MEMO_DAY_YESTERDAY;
    return MEMO_DAY_EARLIER;
}

static bool write_field(const char *text, char *dst, size_t cap)
{
    if (dst == NULL || cap == 0) return false;
    int written = snprintf(dst, cap, "%s", text);
    if (written <= 0 || (size_t)written >= cap) {
        /* Never leave a half-written value behind: the caller asked for either
           the whole string or nothing. */
        dst[0] = '\0';
        return false;
    }
    return true;
}

bool memo_time_clock(int64_t utc_seconds, int32_t tz_offset_min, char *dst, size_t cap)
{
    int64_t local = memo_time_local(utc_seconds, tz_offset_min);
    int64_t day_seconds = local - floor_div(local, SECONDS_PER_DAY) * SECONDS_PER_DAY;
    int hour = (int)(day_seconds / 3600);
    int minute = (int)((day_seconds % 3600) / 60);

    char buf[32];
    snprintf(buf, sizeof(buf), "%02d:%02d", hour, minute);
    return write_field(buf, dst, cap);
}

bool memo_time_date(int64_t utc_seconds, int32_t tz_offset_min, char *dst, size_t cap)
{
    int64_t local = memo_time_local(utc_seconds, tz_offset_min);
    int64_t days = floor_div(local, SECONDS_PER_DAY);
    /* Civil-from-days (Howard Hinnant), 只用整数运算，避免依赖 time.h 的时区库。 */
    int64_t z = days + 719468;
    int64_t era = floor_div(z, 146097);
    int64_t doe = z - era * 146097;
    int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t y = yoe + era * 400;
    int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int64_t mp = (5 * doy + 2) / 153;
    int64_t d = doy - (153 * mp + 2) / 5 + 1;
    int64_t m = mp < 10 ? mp + 3 : mp - 9;
    if (m <= 2) y++;
    (void)y;

    char buf[32];
    snprintf(buf, sizeof(buf), "%02d-%02d", (int)m, (int)d);
    return write_field(buf, dst, cap);
}

bool memo_time_duration(int64_t milliseconds, char *dst, size_t cap)
{
    if (milliseconds < 0) milliseconds = 0;
    int64_t seconds = (milliseconds + 500) / 1000; /* round to nearest second */
    int64_t minutes = seconds / 60;
    int64_t rest = seconds % 60;

    char buf[32];
    snprintf(buf, sizeof(buf), "%lld:%02lld", (long long)minutes, (long long)rest);
    return write_field(buf, dst, cap);
}

bool memo_time_timer(int64_t milliseconds, char *dst, size_t cap)
{
    if (milliseconds < 0) milliseconds = 0;
    int64_t total = milliseconds / 1000;
    int64_t hours = total / 3600;
    int64_t minutes = (total % 3600) / 60;
    int64_t seconds = total % 60;

    char buf[32];
    if (hours > 0) {
        snprintf(buf, sizeof(buf), "%lld:%02lld:%02lld",
                 (long long)hours, (long long)minutes, (long long)seconds);
    } else {
        snprintf(buf, sizeof(buf), "%02lld:%02lld", (long long)minutes, (long long)seconds);
    }
    return write_field(buf, dst, cap);
}
