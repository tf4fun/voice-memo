#pragma once
#include <stdbool.h>

typedef struct {
    int x, y, width, height;
    int title_y, message_y, options_y, hint_y;
} memo_rejection_layout_t;

/* Generated CJK fonts have line heights larger than their nominal pixel size.
 * Reserve three message lines and place choices side by side. */
static inline bool memo_rejection_layout(int width, int height, int label_line,
                                         int body_line, memo_rejection_layout_t *out)
{
    if (out == 0 || width < 160 || height < 100 || label_line <= 0 || body_line <= 0 ||
        label_line > height / 4 || body_line > height / 2) return false;
    memo_rejection_layout_t layout = {.x = 18, .width = width - 39, .title_y = 12};
    layout.message_y = layout.title_y + body_line + 4;
    layout.options_y = layout.message_y + 3 * label_line + 3;
    layout.hint_y = layout.options_y + body_line + 6;
    layout.height = layout.hint_y + label_line + 11;
    if (layout.height > height - 68) return false;
    layout.y = (height - layout.height) / 2;
    *out = layout;
    return true;
}
