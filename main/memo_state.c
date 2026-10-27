// main/memo_state.c —— 状态机的唯一实现。表驱动，方便测穷举。
#include "memo_state.h"

#include <stddef.h>

typedef struct {
    memo_state_t from;
    memo_input_t input;
    memo_state_t to;
    memo_action_t action;
} transition_t;

/* 只列出有意义的迁移；表里没有的组合一律视为「忽略」，返回 MEMO_ACT_NONE。
   ARMING uses MEMO_ARMING_MS, or MEMO_IN_OK_RELEASE to cancel. */
static const transition_t TRANSITIONS[] = {
    /* 列表 */
    {MEMO_STATE_LIST, MEMO_IN_UP_CLICK, MEMO_STATE_LIST, MEMO_ACT_SELECT_PREV},
    {MEMO_STATE_LIST, MEMO_IN_DOWN_CLICK, MEMO_STATE_LIST, MEMO_ACT_SELECT_NEXT},
    {MEMO_STATE_LIST, MEMO_IN_OK_CLICK, MEMO_STATE_DETAIL_COLLAPSED, MEMO_ACT_OPEN_DETAIL},
    {MEMO_STATE_LIST, MEMO_IN_OK_PRESS, MEMO_STATE_ARMING, MEMO_ACT_START_ARMING},
    {MEMO_STATE_LIST, MEMO_IN_UP_HOLD, MEMO_STATE_SETTINGS, MEMO_ACT_OPEN_SETTINGS},
    {MEMO_STATE_LIST, MEMO_IN_DOWN_HOLD, MEMO_STATE_SETTINGS, MEMO_ACT_OPEN_SETTINGS},

    /* 待发：圆环填满就开始录音（手指还按着）；提前松手则放弃 */
    {MEMO_STATE_ARMING, MEMO_IN_ARM_COMPLETE, MEMO_STATE_RECORDING, MEMO_ACT_BEGIN_RECORDING},
    {MEMO_STATE_ARMING, MEMO_IN_OK_RELEASE, MEMO_STATE_LIST, MEMO_ACT_CANCEL_ARMING},

    /* 录音：松手即结束，进入保存/取消选择；到点也走同一条路 */
    {MEMO_STATE_RECORDING, MEMO_IN_OK_RELEASE, MEMO_STATE_CONFIRM, MEMO_ACT_STOP_AND_SAVE},
    {MEMO_STATE_RECORDING, MEMO_IN_RECORD_LIMIT, MEMO_STATE_CONFIRM, MEMO_ACT_STOP_AND_SAVE},

    /* 选择条：上下切换，OK 确认，长按等同取消 */
    {MEMO_STATE_CONFIRM, MEMO_IN_UP_CLICK, MEMO_STATE_CONFIRM, MEMO_ACT_CONFIRM_TOGGLE},
    {MEMO_STATE_CONFIRM, MEMO_IN_DOWN_CLICK, MEMO_STATE_CONFIRM, MEMO_ACT_CONFIRM_TOGGLE},
    {MEMO_STATE_CONFIRM, MEMO_IN_OK_CLICK, MEMO_STATE_LIST, MEMO_ACT_CONFIRM_SAVE},
    {MEMO_STATE_CONFIRM, MEMO_IN_OK_HOLD, MEMO_STATE_LIST, MEMO_ACT_CONFIRM_CANCEL},
    {MEMO_STATE_CONFIRM, MEMO_IN_OK_RELEASE, MEMO_STATE_CONFIRM, MEMO_ACT_NONE},

    /* 详情：OK 单击三步循环 */
    {MEMO_STATE_DETAIL_COLLAPSED, MEMO_IN_OK_CLICK, MEMO_STATE_DETAIL_EXPANDED, MEMO_ACT_EXPAND},
    {MEMO_STATE_DETAIL_COLLAPSED, MEMO_IN_UP_CLICK, MEMO_STATE_DETAIL_COLLAPSED, MEMO_ACT_PREV_MEMO},
    {MEMO_STATE_DETAIL_COLLAPSED, MEMO_IN_DOWN_CLICK, MEMO_STATE_DETAIL_COLLAPSED, MEMO_ACT_NEXT_MEMO},
    {MEMO_STATE_DETAIL_COLLAPSED, MEMO_IN_OK_HOLD, MEMO_STATE_LIST, MEMO_ACT_BACK_TO_LIST},

    {MEMO_STATE_DETAIL_EXPANDED, MEMO_IN_OK_CLICK, MEMO_STATE_PLAYING, MEMO_ACT_PLAY},
    {MEMO_STATE_DETAIL_EXPANDED, MEMO_IN_UP_CLICK, MEMO_STATE_DETAIL_EXPANDED, MEMO_ACT_SCROLL_UP},
    {MEMO_STATE_DETAIL_EXPANDED, MEMO_IN_DOWN_CLICK, MEMO_STATE_DETAIL_EXPANDED, MEMO_ACT_SCROLL_DOWN},
    {MEMO_STATE_DETAIL_EXPANDED, MEMO_IN_OK_HOLD, MEMO_STATE_LIST, MEMO_ACT_BACK_TO_LIST},

    {MEMO_STATE_PLAYING, MEMO_IN_OK_CLICK, MEMO_STATE_PAUSED, MEMO_ACT_PAUSE},
    {MEMO_STATE_PLAYING, MEMO_IN_UP_CLICK, MEMO_STATE_PLAYING, MEMO_ACT_SCROLL_UP},
    {MEMO_STATE_PLAYING, MEMO_IN_DOWN_CLICK, MEMO_STATE_PLAYING, MEMO_ACT_SCROLL_DOWN},
    {MEMO_STATE_PLAYING, MEMO_IN_OK_HOLD, MEMO_STATE_LIST, MEMO_ACT_BACK_TO_LIST},

    {MEMO_STATE_PAUSED, MEMO_IN_OK_CLICK, MEMO_STATE_PLAYING, MEMO_ACT_RESUME},
    {MEMO_STATE_PAUSED, MEMO_IN_UP_CLICK, MEMO_STATE_PAUSED, MEMO_ACT_SCROLL_UP},
    {MEMO_STATE_PAUSED, MEMO_IN_DOWN_CLICK, MEMO_STATE_PAUSED, MEMO_ACT_SCROLL_DOWN},
    {MEMO_STATE_PAUSED, MEMO_IN_OK_HOLD, MEMO_STATE_LIST, MEMO_ACT_BACK_TO_LIST},

    /* 设置 */
    {MEMO_STATE_SETTINGS, MEMO_IN_UP_CLICK, MEMO_STATE_SETTINGS, MEMO_ACT_SETTINGS_PREV},
    {MEMO_STATE_SETTINGS, MEMO_IN_DOWN_CLICK, MEMO_STATE_SETTINGS, MEMO_ACT_SETTINGS_NEXT},
    {MEMO_STATE_SETTINGS, MEMO_IN_OK_CLICK, MEMO_STATE_SUB_PAGE, MEMO_ACT_SUB_PAGE_ENTER},
    /* 进设置是长按上下键，出去也用它：同一个手势负责进出。 */
    {MEMO_STATE_SETTINGS, MEMO_IN_UP_HOLD, MEMO_STATE_LIST, MEMO_ACT_BACK_TO_LIST},
    {MEMO_STATE_SETTINGS, MEMO_IN_DOWN_HOLD, MEMO_STATE_LIST, MEMO_ACT_BACK_TO_LIST},

    /* 子页：OK 保存并返回，长按取消并返回 */
    {MEMO_STATE_SUB_PAGE, MEMO_IN_UP_CLICK, MEMO_STATE_SUB_PAGE, MEMO_ACT_SUB_PAGE_PREV},
    {MEMO_STATE_SUB_PAGE, MEMO_IN_DOWN_CLICK, MEMO_STATE_SUB_PAGE, MEMO_ACT_SUB_PAGE_NEXT},
    {MEMO_STATE_SUB_PAGE, MEMO_IN_OK_CLICK, MEMO_STATE_SETTINGS, MEMO_ACT_SUB_PAGE_SAVE},
    {MEMO_STATE_SUB_PAGE, MEMO_IN_OK_HOLD, MEMO_STATE_SETTINGS, MEMO_ACT_SUB_PAGE_CANCEL},

    /* 配网：只有长按能退出 */
    {MEMO_STATE_PROVISIONING, MEMO_IN_UP_HOLD, MEMO_STATE_SETTINGS, MEMO_ACT_CANCEL_PROVISIONING},
    {MEMO_STATE_PROVISIONING, MEMO_IN_DOWN_HOLD, MEMO_STATE_SETTINGS, MEMO_ACT_CANCEL_PROVISIONING},

    /* 错误页：任意确认都关掉 */
    {MEMO_STATE_ERROR, MEMO_IN_OK_CLICK, MEMO_STATE_LIST, MEMO_ACT_DISMISS_ERROR},
    {MEMO_STATE_ERROR, MEMO_IN_OK_HOLD, MEMO_STATE_LIST, MEMO_ACT_DISMISS_ERROR},

    {MEMO_STATE_UPLOAD_REJECTED, MEMO_IN_UP_CLICK, MEMO_STATE_UPLOAD_REJECTED, MEMO_ACT_REJECT_TOGGLE},
    {MEMO_STATE_UPLOAD_REJECTED, MEMO_IN_DOWN_CLICK, MEMO_STATE_UPLOAD_REJECTED, MEMO_ACT_REJECT_TOGGLE},
    {MEMO_STATE_UPLOAD_REJECTED, MEMO_IN_OK_CLICK, MEMO_STATE_UPLOAD_REJECTED, MEMO_ACT_REJECT_RETRY},
    {MEMO_STATE_UPLOAD_REJECTED, MEMO_IN_OK_HOLD, MEMO_STATE_UPLOAD_REJECTED, MEMO_ACT_REJECT_RETRY},
};

#define TRANSITION_COUNT (sizeof(TRANSITIONS) / sizeof(TRANSITIONS[0]))

void memo_sm_init(memo_sm_t *sm)
{
    if (sm == NULL) return;
    sm->state = MEMO_STATE_LIST;
    sm->expanded = false;
    sm->confirm_save = true; /* 刚录完，默认"保存"更顺手 */
    sm->reject_delete = false;
}

bool memo_sm_open_rejection(memo_sm_t *sm)
{
    if (sm == NULL || (sm->state != MEMO_STATE_LIST &&
                       sm->state != MEMO_STATE_UPLOAD_REJECTED)) return false;
    sm->state = MEMO_STATE_UPLOAD_REJECTED;
    sm->reject_delete = false;
    return true;
}

memo_action_t memo_sm_handle(memo_sm_t *sm, memo_input_t input)
{
    if (sm == NULL) return MEMO_ACT_NONE;

    for (size_t i = 0; i < TRANSITION_COUNT; i++) {
        const transition_t *t = &TRANSITIONS[i];
        if (t->from != sm->state || t->input != input) continue;

        sm->state = t->to;
        if (t->action == MEMO_ACT_REJECT_TOGGLE) {
            sm->reject_delete = !sm->reject_delete;
        } else if (t->action == MEMO_ACT_REJECT_RETRY) {
            if (input == MEMO_IN_OK_HOLD) sm->reject_delete = false;
            return sm->reject_delete ? MEMO_ACT_REJECT_DELETE : MEMO_ACT_REJECT_RETRY;
        }
        if (t->action == MEMO_ACT_STOP_AND_SAVE) {
            sm->confirm_save = true; /* 每次停录都回到默认项 */
        } else if (t->action == MEMO_ACT_CONFIRM_TOGGLE) {
            sm->confirm_save = !sm->confirm_save;
        } else if (t->action == MEMO_ACT_CONFIRM_SAVE) {
            const bool save = sm->confirm_save;
            sm->confirm_save = true;
            return save ? MEMO_ACT_CONFIRM_SAVE : MEMO_ACT_CONFIRM_CANCEL;
        } else if (t->action == MEMO_ACT_CONFIRM_CANCEL) {
            sm->confirm_save = true;
            return MEMO_ACT_CONFIRM_CANCEL;
        }
        if (t->action == MEMO_ACT_EXPAND) {
            sm->expanded = true;
        } else if (t->action == MEMO_ACT_OPEN_DETAIL) {
            sm->expanded = false;
        } else if (t->action == MEMO_ACT_BACK_TO_LIST) {
            sm->expanded = false;
        }
        return t->action;
    }
    return MEMO_ACT_NONE;
}

const char *memo_state_name(memo_state_t state)
{
    switch (state) {
    case MEMO_STATE_LIST: return "list";
    case MEMO_STATE_ARMING: return "arming";
    case MEMO_STATE_RECORDING: return "recording";
    case MEMO_STATE_CONFIRM: return "confirm";
    case MEMO_STATE_DETAIL_COLLAPSED: return "detail_collapsed";
    case MEMO_STATE_DETAIL_EXPANDED: return "detail_expanded";
    case MEMO_STATE_PLAYING: return "playing";
    case MEMO_STATE_PAUSED: return "paused";
    case MEMO_STATE_SETTINGS: return "settings";
    case MEMO_STATE_SUB_PAGE: return "sub_page";
    case MEMO_STATE_PROVISIONING: return "provisioning";
    case MEMO_STATE_ERROR: return "error";
    case MEMO_STATE_UPLOAD_REJECTED: return "upload_rejected";
    default: return "?";
    }
}

const char *memo_action_name(memo_action_t action)
{
    switch (action) {
    case MEMO_ACT_NONE: return "none";
    case MEMO_ACT_SELECT_PREV: return "select_prev";
    case MEMO_ACT_SELECT_NEXT: return "select_next";
    case MEMO_ACT_OPEN_DETAIL: return "open_detail";
    case MEMO_ACT_BACK_TO_LIST: return "back_to_list";
    case MEMO_ACT_START_ARMING: return "start_arming";
    case MEMO_ACT_CANCEL_ARMING: return "cancel_arming";
    case MEMO_ACT_BEGIN_RECORDING: return "begin_recording";
    case MEMO_ACT_STOP_AND_SAVE: return "stop_and_save";
    case MEMO_ACT_CANCEL_RECORDING: return "cancel_recording";
    case MEMO_ACT_CONFIRM_TOGGLE: return "confirm_toggle";
    case MEMO_ACT_CONFIRM_SAVE: return "confirm_save";
    case MEMO_ACT_CONFIRM_CANCEL: return "confirm_cancel";
    case MEMO_ACT_EXPAND: return "expand";
    case MEMO_ACT_PLAY: return "play";
    case MEMO_ACT_PAUSE: return "pause";
    case MEMO_ACT_RESUME: return "resume";
    case MEMO_ACT_STOP_PLAYBACK: return "stop_playback";
    case MEMO_ACT_OPEN_SETTINGS: return "open_settings";
    case MEMO_ACT_SETTINGS_PREV: return "settings_prev";
    case MEMO_ACT_SETTINGS_NEXT: return "settings_next";
    case MEMO_ACT_SUB_PAGE_ENTER: return "sub_page_enter";
    case MEMO_ACT_SUB_PAGE_PREV: return "sub_page_prev";
    case MEMO_ACT_SUB_PAGE_NEXT: return "sub_page_next";
    case MEMO_ACT_SUB_PAGE_SAVE: return "sub_page_save";
    case MEMO_ACT_SUB_PAGE_CANCEL: return "sub_page_cancel";
    case MEMO_ACT_CANCEL_PROVISIONING: return "cancel_provisioning";
    case MEMO_ACT_DISMISS_ERROR: return "dismiss_error";
    case MEMO_ACT_NEXT_MEMO: return "next_memo";
    case MEMO_ACT_PREV_MEMO: return "prev_memo";
    case MEMO_ACT_SCROLL_UP: return "scroll_up";
    case MEMO_ACT_SCROLL_DOWN: return "scroll_down";
    case MEMO_ACT_REJECT_TOGGLE: return "reject_toggle";
    case MEMO_ACT_REJECT_RETRY: return "reject_retry";
    case MEMO_ACT_REJECT_DELETE: return "reject_delete";
    default: return "?";
    }
}

const char *memo_input_name(memo_input_t input)
{
    switch (input) {
    case MEMO_IN_UP_CLICK: return "up_click";
    case MEMO_IN_DOWN_CLICK: return "down_click";
    case MEMO_IN_OK_CLICK: return "ok_click";
    case MEMO_IN_OK_HOLD: return "ok_hold";
    case MEMO_IN_OK_PRESS: return "ok_press";
    case MEMO_IN_OK_RELEASE: return "ok_release";
    case MEMO_IN_UP_HOLD: return "up_hold";
    case MEMO_IN_DOWN_HOLD: return "down_hold";
    case MEMO_IN_ARM_COMPLETE: return "arm_complete";
    case MEMO_IN_RECORD_LIMIT: return "record_limit";
    default: return "?";
    }
}

bool memo_state_keeps_screen_on(memo_state_t state)
{
    return state == MEMO_STATE_ARMING || state == MEMO_STATE_RECORDING ||
           state == MEMO_STATE_CONFIRM || state == MEMO_STATE_PLAYING ||
           state == MEMO_STATE_PROVISIONING || state == MEMO_STATE_UPLOAD_REJECTED;
}

bool memo_state_is_recording(memo_state_t state)
{
    return state == MEMO_STATE_RECORDING;
}
