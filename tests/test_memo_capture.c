#include <assert.h>
#include <stdio.h>
#include "memo_capture.h"

int main(void)
{
    memo_capture_hold_t g = {0};
    memo_capture_press(&g, 1000000, true);
    assert(!memo_capture_preview(&g, 999999));
    assert(!memo_capture_preview(&g, 1179999));
    memo_capture_release(&g, 1179999);
    assert(!g.active && !g.consume_click);
    memo_capture_press(&g, 2000000, true);
    assert(memo_capture_preview(&g, 2180000));
    memo_capture_release(&g, 2180000);
    assert(g.consume_click && !memo_capture_preview(&g, 3000000));
    // A fresh OK press restores click actions outside capture as well.
    memo_capture_press(&g, 3000000, false);
    assert(!g.consume_click && !memo_capture_preview(&g, 4000000));
    memo_capture_commit(&g);
    memo_capture_release(&g, 3000001);
    assert(g.consume_click);
    assert(memo_capture_same_hold(4,4,true));
    assert(!memo_capture_same_hold(4,5,true)); // Release and repress during preparation.
    assert(!memo_capture_same_hold(4,4,false));

    memo_capture_wave_t w = {0};
    for (unsigned i = 0; i < MEMO_WAVE_SAMPLES + 5; i++) memo_capture_wave_push(&w, i);
    for (unsigned i = 0; i < MEMO_WAVE_SAMPLES; i++) assert(w.level[i] == i + 5);
    memo_capture_wave_push(&w, 1000);
    assert(w.level[MEMO_WAVE_SAMPLES - 1] == 100);
    assert(memo_capture_wave_height(0) == 2);
    assert(memo_capture_wave_height(100) == 56);
    assert(memo_capture_wave_height(1000) == 56);
    for (unsigned i = 0; i <= 100; i++) {
        int h = memo_capture_wave_height(i);
        assert(h >= 2 && h <= 56 && 188 - h / 2 >= 160 && 188 + (h + 1) / 2 <= 216);
    }
    puts("memo capture tap/hold boundary, tail suppression and bounded input history: PASS");
}
