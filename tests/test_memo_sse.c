#include <assert.h>
#include <stdio.h>
#include "memo_sse.h"
static int events;
static char received[MEMO_SSE_CAP];
static void emit(const char *text, void *ctx)
{ (void)ctx; events++; snprintf(received, sizeof(received), "%s", text); }
int main(void)
{
    const char *event = "event: memo\r\ndata: {\"id\":\"test\"}\r\n\r\n";
    memo_sse_t parser;
    for (size_t split = 0; split <= strlen(event); split++) {
        memo_sse_reset(&parser); events = 0;
        assert(!memo_sse_feed(&parser, event, split, emit, NULL));
        assert(!memo_sse_feed(&parser, event + split, strlen(event) - split, emit, NULL));
        assert(events == 1 && strcmp(received, "{\"id\":\"test\"}") == 0);
    }
    memo_sse_reset(&parser); events = 0;
    const char *multi = ": keepalive\n\ndata: first\ndata: second\n\n";
    assert(!memo_sse_feed(&parser, multi, strlen(multi), emit, NULL));
    assert(events == 1 && strcmp(received, "first\nsecond") == 0);
    char large[800]; memset(large, 'x', sizeof(large));
    assert(memo_sse_feed(&parser, large, sizeof(large), emit, NULL));
    assert(!memo_sse_feed(&parser, "\n\n", 2, emit, NULL));
    assert(events == 1); /* Never apply a valid-looking truncated event. */
    assert(!memo_sse_feed(&parser, event, strlen(event), emit, NULL) && events == 2);
    memo_sse_feed(&parser, "data: half", 10, emit, NULL);
    memo_sse_reset(&parser); /* Reconnect discards partial JSON/data state. */
    memo_sse_feed(&parser, event, strlen(event), emit, NULL);
    assert(events == 3 && strcmp(received, "{\"id\":\"test\"}") == 0);
    puts("memo SSE fragmentation, multiline, overflow and reconnect reset: PASS");
}
