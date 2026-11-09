// Application idle policy and shutdown ordering, independent of ESP-IDF/LVGL.
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    int64_t activity_ms;
    bool blank;
    bool consuming_wake;
    bool wake_released;
    int64_t blank_since_ms; // Valid while blank; deep-sleep delay starts here.
} memo_power_t;

typedef enum { MEMO_POWER_ON, MEMO_POWER_BLANK, MEMO_POWER_SLEEP } memo_power_action_t;

// Consume the entire first gesture, including CLICK delivered after RELEASE.
// Call for every physical event, even keys that have no application action.
static inline bool memo_power_key(memo_power_t *p, int64_t now_ms, bool press, bool release)
{
    p->activity_ms = now_ms;
    if (p->blank) {
        p->blank = false;
        p->blank_since_ms = 0;
        p->consuming_wake = true;
        p->wake_released = false;
    } else if (p->consuming_wake && p->wake_released && press) {
        p->consuming_wake = false;
    }
    if (p->consuming_wake && release) p->wake_released = true;
    return p->consuming_wake;
}

static inline memo_power_action_t memo_power_poll(memo_power_t *p, int64_t now_ms,
                                                 unsigned screen_s, unsigned sleep_min,
                                                 bool keep_on, bool block_sleep)
{
    if (keep_on || now_ms < p->activity_ms) p->activity_ms = now_ms;
    const int64_t idle = now_ms - p->activity_ms;
    const bool blank = !keep_on && screen_s && idle >= (int64_t)screen_s * 1000;
    if (!blank) {
        p->blank = false;
        p->blank_since_ms = 0;
        return MEMO_POWER_ON;
    }
    // Start at the actual first blank decision, including after a delayed poll.
    if (!p->blank || now_ms < p->blank_since_ms) p->blank_since_ms = now_ms;
    p->blank = true;
    if (!block_sleep && sleep_min && now_ms - p->blank_since_ms >= (int64_t)sleep_min * 60000)
        return MEMO_POWER_SLEEP;
    return MEMO_POWER_BLANK;
}

typedef struct {
    int (*stop_events)(void *), (*stop_upload)(void *), (*stop_player)(void *);
    bool (*safe)(void *); // Recheck pending data, physical keys and queued input.
    bool (*lock_display)(void *);
    int (*arm_wake)(void *);
    void (*unlock_display)(void *), (*recover_workers)(void *);
    void (*release_network)(void *);
    int (*release_buttons)(void *), (*battery_sleep)(void *), (*audio_sleep)(void *);
    int (*audio_pins)(void *), (*i2c_pins)(void *), (*display_sleep)(void *);
    void (*sleep)(void *), (*restart)(void *);
    void *ctx;
} memo_power_ops_t;

// After releasing buttons, peripherals are terminal: never return to the UI.
// Peripheral suspend failures still finish shutdown; a lost wake source or
// button teardown failure requires restart. The real sleep/restart do not return.
static inline void memo_power_shutdown(const memo_power_ops_t *ops)
{
    void *ctx = ops->ctx;
    const int events = ops->stop_events(ctx), upload = ops->stop_upload(ctx);
    const int player = ops->stop_player(ctx);
    if (events || upload || player || !ops->safe(ctx)) {
        ops->recover_workers(ctx);
        return;
    }
    if (!ops->lock_display(ctx)) {
        ops->recover_workers(ctx);
        return;
    }
    if (!ops->safe(ctx) || ops->arm_wake(ctx)) {
        ops->unlock_display(ctx);
        ops->recover_workers(ctx);
        return;
    }
    ops->release_network(ctx);
    if (ops->release_buttons(ctx)) {
        ops->restart(ctx);
        return;
    }
    ops->battery_sleep(ctx);
    ops->audio_sleep(ctx);
    ops->audio_pins(ctx);
    ops->i2c_pins(ctx);
    ops->display_sleep(ctx);
    ops->sleep(ctx);
    ops->restart(ctx);
}
