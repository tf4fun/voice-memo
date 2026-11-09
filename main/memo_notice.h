// App-owned transient feedback, independent of render retries and LVGL locks.
#pragma once
#include <stdint.h>
#include <stddef.h>
typedef struct {
    const char *text; // Static UI literal owned by the application.
    int64_t until_us;
} memo_notice_t;
static inline void memo_notice_show(memo_notice_t *notice, const char *text, int64_t now_us)
{
    *notice = (memo_notice_t){.text=text, .until_us=now_us+3000000};
}
static inline const char *memo_notice_text(const memo_notice_t *notice, int64_t now_us)
{
    return notice->text != NULL && now_us < notice->until_us ? notice->text : NULL;
}
