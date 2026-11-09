// Bounded SSE framing, independent of LVGL/ESP-IDF. Overflow requests a fresh
// snapshot and discards the entire event, never a truncated JSON prefix.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#define MEMO_SSE_CAP 512
typedef struct {
    char line[MEMO_SSE_CAP], data[MEMO_SSE_CAP];
    size_t line_len, data_len;
    bool overflow;
} memo_sse_t;
typedef void (*memo_sse_event_fn)(const char *json, void *ctx);
static inline void memo_sse_reset(memo_sse_t *p) { memset(p, 0, sizeof(*p)); }
static inline bool memo_sse_feed(memo_sse_t *p, const char *bytes, size_t len,
                                 memo_sse_event_fn emit, void *ctx)
{
    bool reconcile = false;
    for (size_t i = 0; i < len; i++) {
        char c = bytes[i];
        if (c == '\r') continue;
        if (c != '\n') {
            if (c == '\0' || p->line_len + 1 >= sizeof(p->line)) {
                p->overflow = true; reconcile = true;
            } else p->line[p->line_len++] = c;
            continue;
        }
        p->line[p->line_len] = '\0';
        if (p->line_len == 0) {
            if (!p->overflow && p->data_len > 0) emit(p->data, ctx);
            p->data_len = 0; p->data[0] = '\0'; p->overflow = false;
        } else if (!p->overflow && strncmp(p->line, "data:", 5) == 0) {
            const char *value = p->line + 5;
            if (*value == ' ') value++;
            size_t n = strlen(value), separator = p->data_len > 0 ? 1 : 0;
            if (p->data_len + separator + n + 1 > sizeof(p->data)) {
                p->overflow = true; reconcile = true;
            } else {
                if (separator) p->data[p->data_len++] = '\n';
                memcpy(p->data + p->data_len, value, n + 1); p->data_len += n;
            }
        }
        p->line_len = 0;
    }
    return reconcile;
}
