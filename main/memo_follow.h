// Proportional transcript following. Server offsets remain UTF-8 byte boundaries;
// the target is only used for comparison, never as an HTTP request offset.
#pragma once
#include <stdint.h>

static inline uint32_t memo_follow_target(uint32_t position, uint32_t duration, uint32_t total)
{
    if (!duration || !total) return 0;
    if (position >= duration) return total - 1;
    return (uint32_t)((uint64_t)position * total / duration);
}

static inline int memo_follow_scroll(uint32_t target, uint32_t offset, uint32_t end, int extent)
{
    if (extent <= 0 || end <= offset || target <= offset) return 0;
    if (target >= end - 1) return extent;
    return (int)((uint64_t)(target - offset) * (unsigned)extent / (end - offset - 1));
}
