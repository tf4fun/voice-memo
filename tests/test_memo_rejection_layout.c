#include <assert.h>
#include <stdio.h>
#include "memo_rejection_layout.h"

int main(void)
{
    memo_rejection_layout_t layout;
    assert(memo_rejection_layout(240, 320, 31, 38, &layout));
    for (int label = 12; label <= 40; label++) {
        for (int body = 16; body <= 48; body++) {
            if (!memo_rejection_layout(240, 320, label, body, &layout)) continue;
            assert(layout.y >= 34 && layout.y + layout.height <= 286);
            assert(layout.x >= 8 && layout.x + layout.width + 3 <= 232);
            assert(layout.title_y + body < layout.message_y);
            assert(layout.message_y + 3 * label < layout.options_y);
            assert(layout.options_y + body < layout.hint_y);
            assert(layout.hint_y + label < layout.height - 2);
        }
    }
    assert(!memo_rejection_layout(240, 320, 0, 38, &layout));
    assert(!memo_rejection_layout(240, 320, 31, 0, &layout));
    assert(!memo_rejection_layout(240, 320, 60, 70, &layout));
    assert(!memo_rejection_layout(100, 320, 31, 38, &layout));
    assert(!memo_rejection_layout(240, 100, 31, 38, &layout));
    assert(!memo_rejection_layout(240, 320, 31, 38, NULL));
    puts("memo rejection font-metric layout: PASS");
    return 0;
}
