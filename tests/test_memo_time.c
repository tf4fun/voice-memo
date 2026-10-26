/* 宿主测试：时区、日分组、时长格式。 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "memo_time.h"

#define BEIJING 480
#define NEW_YORK (-300)

static void expect_str(const char *got, const char *want, const char *what)
{
    if (strcmp(got, want) != 0) {
        printf("FAIL %s: got \"%s\", want \"%s\"\n", what, got, want);
        assert(0);
    }
}

/* 2026-10-26 00:32:00 UTC */
#define T_0032_UTC 1792974720LL

static void test_clock(void)
{
    char buf[16];

    /* 同一时刻在两个时区显示不同。 */
    assert(memo_time_clock(T_0032_UTC, BEIJING, buf, sizeof(buf)));
    expect_str(buf, "08:32", "beijing 08:32");

    assert(memo_time_clock(T_0032_UTC, NEW_YORK, buf, sizeof(buf)));
    expect_str(buf, "19:32", "new york previous evening");

    assert(memo_time_clock(0, 0, buf, sizeof(buf)));
    expect_str(buf, "00:00", "epoch midnight utc");

    /* 容量不足：写空串并报失败，不留半截。 */
    char tiny[3];
    assert(!memo_time_clock(T_0032_UTC, BEIJING, tiny, sizeof(tiny)));
    expect_str(tiny, "", "insufficient capacity");
}

static void test_date(void)
{
    char buf[16];
    assert(memo_time_date(T_0032_UTC, BEIJING, buf, sizeof(buf)));
    expect_str(buf, "10-26", "beijing date");

    /* UTC 那边还是 25 日，北京已经是 26 日。 */
    assert(memo_time_date(T_0032_UTC, 0, buf, sizeof(buf)));
    expect_str(buf, "10-26", "utc date");

    /* 跨年边界：2026-01-01 00:30 UTC 在北京是 08:30 元旦。 */
    const long long new_year = 1798761000LL; /* 2026-01-01T00:30:00Z */
    assert(memo_time_date(new_year, BEIJING, buf, sizeof(buf)));
    expect_str(buf, "01-01", "new year in beijing");
}

static void test_day_classification(void)
{
    const int64_t now = T_0032_UTC; /* 北京 10-26 08:32 */

    assert(memo_time_classify(now, now - 3600, BEIJING) == MEMO_DAY_TODAY);
    assert(memo_time_classify(now, now - 86400, BEIJING) == MEMO_DAY_YESTERDAY);
    assert(memo_time_classify(now, now - 3 * 86400, BEIJING) == MEMO_DAY_EARLIER);

    /* 同一时刻、不同时区会落到不同的本地日：UTC 10-25 20:00 是北京 10-26 04:00。 */
    const int64_t evening_utc = now - (4 * 3600 + 32 * 60);
    assert(memo_time_classify(now, evening_utc, BEIJING) == MEMO_DAY_TODAY);
    assert(memo_time_classify(now, evening_utc, 0) == MEMO_DAY_YESTERDAY);
}

static void test_duration(void)
{
    char buf[16];

    assert(memo_time_duration(0, buf, sizeof(buf)));
    expect_str(buf, "0:00", "zero");

    assert(memo_time_duration(9000, buf, sizeof(buf)));
    expect_str(buf, "0:09", "nine seconds");

    assert(memo_time_duration(84000, buf, sizeof(buf)));
    expect_str(buf, "1:24", "eighty-four seconds");

    assert(memo_time_duration(-5000, buf, sizeof(buf)));
    expect_str(buf, "0:00", "negative clamps to zero");

    assert(memo_time_duration(725000, buf, sizeof(buf)));
    expect_str(buf, "12:05", "twelve minutes");
}

static void test_timer(void)
{
    char buf[16];

    assert(memo_time_timer(0, buf, sizeof(buf)));
    expect_str(buf, "00:00", "timer zero");

    assert(memo_time_timer(83000, buf, sizeof(buf)));
    expect_str(buf, "01:23", "timer 83s");

    assert(memo_time_timer(300000, buf, sizeof(buf)));
    expect_str(buf, "05:00", "five minute cap");

    assert(memo_time_timer(3723000, buf, sizeof(buf)));
    expect_str(buf, "1:02:03", "past an hour");
}

int main(void)
{
    test_clock();
    test_date();
    test_day_classification();
    test_duration();
    test_timer();
    puts("memo time: PASS");
    return 0;
}
