#include <assert.h>
#include <stdio.h>
#include "memo_row_tone.h"
int main(void)
{
    assert(memo_row_tone(true, true, "", false) == MEMO_TONE_CORAL);
    assert(memo_row_tone(true, false, "", false) == MEMO_TONE_MUTED);
    assert(memo_row_tone(false, false, "failed", false) == MEMO_TONE_CORAL);
    assert(memo_row_tone(false, false, "failed", true) == MEMO_TONE_CORAL);
    assert(memo_row_tone(false, false, "uploaded", false) == MEMO_TONE_MUTED);
    assert(memo_row_tone(false, false, "transcribing", false) == MEMO_TONE_MUTED);
    assert(memo_row_tone(false, false, "ready", false) == MEMO_TONE_MUTED);
    assert(memo_row_tone(false, false, "ready", true) == MEMO_TONE_INK);
    assert(memo_row_tone(false, false, "bogus", true) == MEMO_TONE_MUTED);
    assert(memo_row_tone(false, false, NULL, false) == MEMO_TONE_MUTED);

    assert(memo_row_accent(true, true, "", false) == MEMO_ACCENT_CORAL);
    assert(memo_row_accent(true, false, "", false) == MEMO_ACCENT_SUN);
    assert(memo_row_accent(false, false, "failed", true) == MEMO_ACCENT_CORAL);
    assert(memo_row_accent(false, false, "uploaded", false) == MEMO_ACCENT_SUN);
    assert(memo_row_accent(false, false, "transcribing", false) == MEMO_ACCENT_SUN);
    assert(memo_row_accent(false, false, "ready", true) == MEMO_ACCENT_MINT);
    assert(memo_row_accent(false, false, "ready", false) == MEMO_ACCENT_GRAY);
    assert(memo_row_accent(false, false, "bogus", true) == MEMO_ACCENT_GRAY);
    assert(memo_row_accent(false, false, NULL, true) == MEMO_ACCENT_GRAY);
    puts("memo row tone/accent: failures coral, work sun, transcripts mint/ink, rest muted/gray: PASS");
}
