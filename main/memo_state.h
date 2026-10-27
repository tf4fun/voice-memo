// main/memo_state.h —— 界面状态机（纯逻辑，不依赖 ESP-IDF / LVGL）。
//
// 状态与输入到动作的映射集中在这里，UI 只负责渲染状态、执行动作。
// 这样按钮语义可以完全在宿主上测，不必上真机。
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    MEMO_STATE_LIST = 0,
    MEMO_STATE_ARMING,
    MEMO_STATE_RECORDING,
    MEMO_STATE_CONFIRM,
    MEMO_STATE_DETAIL_COLLAPSED,
    MEMO_STATE_DETAIL_EXPANDED,
    MEMO_STATE_PLAYING,
    MEMO_STATE_PAUSED,
    MEMO_STATE_SETTINGS,
    MEMO_STATE_SUB_PAGE,
    MEMO_STATE_PROVISIONING,
    MEMO_STATE_ERROR,
    MEMO_STATE_UPLOAD_REJECTED,
    MEMO_STATE_COUNT,
} memo_state_t;

typedef enum {
    MEMO_IN_UP_CLICK = 0,
    MEMO_IN_DOWN_CLICK,
    MEMO_IN_OK_CLICK,
    MEMO_IN_OK_HOLD,
    MEMO_IN_OK_PRESS,
    MEMO_IN_OK_RELEASE,
    MEMO_IN_UP_HOLD,
    MEMO_IN_DOWN_HOLD,
    MEMO_IN_ARM_COMPLETE,
    MEMO_IN_RECORD_LIMIT,
    MEMO_IN_COUNT,
} memo_input_t;

typedef enum {
    MEMO_ACT_NONE = 0,
    MEMO_ACT_SELECT_PREV,
    MEMO_ACT_SELECT_NEXT,
    MEMO_ACT_OPEN_DETAIL,
    MEMO_ACT_BACK_TO_LIST,
    MEMO_ACT_START_ARMING,
    MEMO_ACT_CANCEL_ARMING,
    MEMO_ACT_BEGIN_RECORDING,
    MEMO_ACT_STOP_AND_SAVE,
    MEMO_ACT_CANCEL_RECORDING,
    MEMO_ACT_CONFIRM_TOGGLE,
    MEMO_ACT_CONFIRM_SAVE,
    MEMO_ACT_CONFIRM_CANCEL,
    MEMO_ACT_EXPAND,
    MEMO_ACT_PLAY,
    MEMO_ACT_PAUSE,
    MEMO_ACT_RESUME,
    MEMO_ACT_STOP_PLAYBACK,
    MEMO_ACT_OPEN_SETTINGS,
    MEMO_ACT_SETTINGS_PREV,
    MEMO_ACT_SETTINGS_NEXT,
    MEMO_ACT_SUB_PAGE_ENTER,
    MEMO_ACT_SUB_PAGE_PREV,
    MEMO_ACT_SUB_PAGE_NEXT,
    MEMO_ACT_SUB_PAGE_SAVE,
    MEMO_ACT_SUB_PAGE_CANCEL,
    MEMO_ACT_CANCEL_PROVISIONING,
    MEMO_ACT_DISMISS_ERROR,
    MEMO_ACT_NEXT_MEMO,
    MEMO_ACT_PREV_MEMO,
    MEMO_ACT_SCROLL_UP,
    MEMO_ACT_SCROLL_DOWN,
    MEMO_ACT_REJECT_TOGGLE,
    MEMO_ACT_REJECT_RETRY,
    MEMO_ACT_REJECT_DELETE,
    MEMO_ACT_COUNT,
} memo_action_t;

typedef struct {
    memo_state_t state;
    /* Latches the "expanded once" rule: returning from detail resets it. */
    bool expanded;
    /* 录音结束后的选择条当前落在"保存"(true)还是"取消"(false)。 */
    bool confirm_save;
    bool reject_delete; /* Destructive choice is never the default. */
} memo_sm_t;

/* 圆环从"按下"开始填，填满即开始录音；手指仍按着，所以这是一个动作。
   这个值只算一次：长按判定(500 ms)与它不叠加。 */
#define MEMO_ARMING_MS 1000

/* A single memo is capped here; the UI warns from 4:30 and stops at 5:00. */
#define MEMO_RECORD_LIMIT_MS (5 * 60 * 1000)
#define MEMO_RECORD_WARN_MS (4 * 60 * 1000 + 30 * 1000)

void memo_sm_init(memo_sm_t *sm);

/* Show on the list or reset for the next rejected ID; never interrupt audio. */
bool memo_sm_open_rejection(memo_sm_t *sm);

/* Apply one input. Returns the single action the UI must perform. */
memo_action_t memo_sm_handle(memo_sm_t *sm, memo_input_t input);

/* Human-readable names, used by tests and logs. */
const char *memo_state_name(memo_state_t state);
const char *memo_action_name(memo_action_t action);
const char *memo_input_name(memo_input_t input);

/* Whether the screen must stay on in this state. */
bool memo_state_keeps_screen_on(memo_state_t state);

/* Whether audio is being captured in this state. */
bool memo_state_is_recording(memo_state_t state);
