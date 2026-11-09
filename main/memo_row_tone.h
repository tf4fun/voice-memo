// List-row snippet tone and accent colour, independent of RTOS/LVGL.
#pragma once
#include <stdbool.h>
#include <string.h>

typedef enum { MEMO_TONE_INK, MEMO_TONE_MUTED, MEMO_TONE_CORAL } memo_row_tone_t;

// Placeholder texts read muted, failures coral, real transcripts ink.
static inline memo_row_tone_t memo_row_tone(bool local, bool bad_or_legacy,
                                            const char *status, bool has_snippet)
{
    if (local) {
        return bad_or_legacy ? MEMO_TONE_CORAL : MEMO_TONE_MUTED;
    }
    if (status != NULL && strcmp(status, "failed") == 0) return MEMO_TONE_CORAL;
    if (status != NULL && strcmp(status, "ready") == 0 && has_snippet) return MEMO_TONE_INK;
    return MEMO_TONE_MUTED;
}

typedef enum { MEMO_ACCENT_MINT, MEMO_ACCENT_SUN, MEMO_ACCENT_CORAL, MEMO_ACCENT_GRAY } memo_row_accent_t;

// Leading strip: mint = 已有转写, sun = 进行中/待处理, coral = 失败, gray = 其他.
static inline memo_row_accent_t memo_row_accent(bool local, bool bad_or_legacy,
                                                const char *status, bool has_snippet)
{
    if (local) {
        return bad_or_legacy ? MEMO_ACCENT_CORAL : MEMO_ACCENT_SUN;
    }
    if (status != NULL && strcmp(status, "failed") == 0) return MEMO_ACCENT_CORAL;
    if (status != NULL && strcmp(status, "ready") == 0) {
        return has_snippet ? MEMO_ACCENT_MINT : MEMO_ACCENT_GRAY;
    }
    if (status != NULL && (strcmp(status, "uploaded") == 0 || strcmp(status, "transcribing") == 0)) {
        return MEMO_ACCENT_SUN;
    }
    return MEMO_ACCENT_GRAY;
}
