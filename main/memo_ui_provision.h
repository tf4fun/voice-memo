// main/memo_ui_provision.h —— 配网页：显示广播名与连接状态。
#pragma once

#include "lvgl.h"

void memo_ui_provision_build(void);
void memo_ui_provision_destroy(void); /* Inactive screen; hold LVGL lock. */
void memo_ui_provision_refresh(void); /* 由定时器轮询调用 */
lv_obj_t *memo_ui_provision_screen(void);
