#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "memo_notice.h"
int main(void)
{
    memo_notice_t notice = {0};
    assert(memo_notice_text(&notice, 0) == NULL);
    memo_notice_show(&notice, "pending", 1000000);
    // Repeated reconciliation, including after a missed render, cannot consume it.
    for (int64_t now=1000000; now<4000000; now+=50000)
        assert(!strcmp(memo_notice_text(&notice, now), "pending"));
    assert(memo_notice_text(&notice, 4000000) == NULL);
    memo_notice_show(&notice, "busy", 5000000);
    memo_notice_show(&notice, "unavailable", 6000000);
    assert(!strcmp(memo_notice_text(&notice, 8000000), "unavailable"));
    assert(memo_notice_text(&notice, 9000000) == NULL);
    puts("memo transient notice tests passed");
}
