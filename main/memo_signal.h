// Digital input hints, not an acoustic calibration or microphone diagnosis.
#pragma once
#include <stddef.h>
#include <stdint.h>

enum { MEMO_SIGNAL_NORMAL, MEMO_SIGNAL_QUIET, MEMO_SIGNAL_CLIPPED };
typedef struct { unsigned quiet_ms, clipped_ms; } memo_signal_t;
typedef struct { uint8_t level, hint; } memo_signal_reading_t;

static inline memo_signal_reading_t memo_signal_read(memo_signal_t *s, const int16_t *pcm,
                                                    size_t count, unsigned frame_ms)
{
    if (!count) return (memo_signal_reading_t){0};
    uint64_t squares = 0;
    size_t clipped = 0;
    for (size_t i=0; i<count; i++) {
        const int32_t v = pcm[i];
        squares += (uint64_t)((int64_t)v*v);
        if (v >= 32760 || v <= -32760) clipped++;
    }
    uint64_t value = squares/count, root = 0, bit = (uint64_t)1 << 30;
    while (bit > value) bit >>= 2;
    while (bit) {
        if (value >= root+bit) { value -= root+bit; root = (root>>1)+bit; }
        else root >>= 1;
        bit >>= 2;
    }
    // Saturating durations use captured frames, not UI sampling intervals.
    s->quiet_ms = root < 80 ? (s->quiet_ms < 3000 ? s->quiet_ms+frame_ms : 3000) : 0;
    s->clipped_ms = clipped*100 >= count ?
        (s->clipped_ms < 180 ? s->clipped_ms+frame_ms : 180) : 0;
    const unsigned level = (unsigned)(root*100/3000);
    return (memo_signal_reading_t){.level=level>100 ? 100 : level,
        .hint=s->clipped_ms>=180 ? MEMO_SIGNAL_CLIPPED :
              s->quiet_ms>=3000 ? MEMO_SIGNAL_QUIET : MEMO_SIGNAL_NORMAL};
}
