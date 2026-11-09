// Capture gesture and signal-history policies, independent of LVGL/ESP-IDF.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define MEMO_CAPTURE_PREVIEW_MS 180
#define MEMO_WAVE_SAMPLES 20

typedef struct {
    int64_t pressed_us;
    bool active;
    bool consume_click;
} memo_capture_hold_t;

static inline void memo_capture_press(memo_capture_hold_t *g, int64_t now_us, bool on_list)
{
    *g = (memo_capture_hold_t){.pressed_us = now_us, .active = on_list};
}

static inline bool memo_capture_preview(const memo_capture_hold_t *g, int64_t now_us)
{
    return g->active && now_us >= g->pressed_us &&
           now_us - g->pressed_us >= MEMO_CAPTURE_PREVIEW_MS * 1000;
}

static inline void memo_capture_release(memo_capture_hold_t *g, int64_t now_us)
{
    if (memo_capture_preview(g, now_us)) g->consume_click = true;
    g->active = false;
}

// Retain the tail suppression until a fresh OK press, even on a failed start.
static inline void memo_capture_commit(memo_capture_hold_t *g)
{
    g->consume_click = true;
}

/* Callback-side release epoch invalidates a hold even if release/repress events
 * are still queued behind preparation and the physical key is down again. */
static inline bool memo_capture_same_hold(uint32_t expected, uint32_t released, bool down)
{
    return down && expected == released;
}

typedef struct { uint8_t level[MEMO_WAVE_SAMPLES]; } memo_capture_wave_t;

static inline void memo_capture_wave_push(memo_capture_wave_t *w, unsigned level)
{
    memmove(w->level, w->level + 1, MEMO_WAVE_SAMPLES - 1);
    w->level[MEMO_WAVE_SAMPLES - 1] = level > 100 ? 100 : level;
}

static inline int memo_capture_wave_height(unsigned level)
{
    if (level > 100) level = 100;
    return 2 + (int)(level * 54 / 100);
}
