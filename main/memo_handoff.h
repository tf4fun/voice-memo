// Network ownership coordinator; callbacks own platform operations, no UI/RTOS.
#pragma once
#include <stdbool.h>
typedef enum { MEMO_HANDOFF_NORMAL, MEMO_HANDOFF_ACTIVE, MEMO_HANDOFF_EXITING, MEMO_HANDOFF_RESUMING } memo_handoff_phase_t;
typedef struct {
    int (*stop_events)(void *), (*stop_upload)(void *);
    void (*release_discovery)(void *);
    int (*suspend_network)(void *);
    int (*start_provision)(void *), (*stop_provision)(void *), (*resume)(void *);
    void *ctx;
} memo_handoff_ops_t;
typedef struct { memo_handoff_phase_t phase; } memo_handoff_t;
static inline bool memo_handoff_poll(memo_handoff_t *h, const memo_handoff_ops_t *ops)
{
    if (h->phase == MEMO_HANDOFF_EXITING) {
        if (ops->stop_provision(ops->ctx) != 0) return false;
        h->phase = MEMO_HANDOFF_RESUMING;
    }
    if (h->phase == MEMO_HANDOFF_RESUMING) {
        if (ops->resume(ops->ctx) != 0) return false;
        h->phase = MEMO_HANDOFF_NORMAL;
    }
    return h->phase == MEMO_HANDOFF_NORMAL;
}
static inline bool memo_handoff_begin(memo_handoff_t *h, const memo_handoff_ops_t *ops)
{
    if (h->phase != MEMO_HANDOFF_NORMAL) return false;
    int events = ops->stop_events(ops->ctx), upload = ops->stop_upload(ops->ctx);
    if (events != 0 || upload != 0) {
        h->phase = MEMO_HANDOFF_RESUMING;
        memo_handoff_poll(h, ops);
        return false;
    }
    ops->release_discovery(ops->ctx);
    if (ops->suspend_network(ops->ctx) != 0) {
        h->phase = MEMO_HANDOFF_RESUMING;
        memo_handoff_poll(h, ops);
        return false;
    }
    h->phase = MEMO_HANDOFF_ACTIVE;
    if (ops->start_provision(ops->ctx) == 0) return true;
    h->phase = MEMO_HANDOFF_EXITING;
    memo_handoff_poll(h, ops);
    return false;
}
static inline bool memo_handoff_end(memo_handoff_t *h, const memo_handoff_ops_t *ops)
{
    if (h->phase == MEMO_HANDOFF_ACTIVE) h->phase = MEMO_HANDOFF_EXITING;
    return memo_handoff_poll(h, ops);
}
