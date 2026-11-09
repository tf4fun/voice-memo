/* 宿主测试：界面状态机。穷举每个状态下的每种输入，确认「忽略」是显式的。 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "memo_state.h"

static void expect(memo_state_t from, memo_input_t input, memo_state_t to, memo_action_t action)
{
    memo_sm_t sm = {.state = from, .confirm_save = true};
    memo_action_t got = memo_sm_handle(&sm, input);
    if (sm.state != to || got != action) {
        printf("FAIL %s + %s: got state=%s action=%s, want state=%s action=%s\n",
               memo_state_name(from), memo_input_name(input),
               memo_state_name(sm.state), memo_action_name(got),
               memo_state_name(to), memo_action_name(action));
        assert(0);
    }
}

static void expect_ignored(memo_state_t from, memo_input_t input)
{
    expect(from, input, from, MEMO_ACT_NONE);
}

static void test_list_navigation(void)
{
    expect(MEMO_STATE_LIST, MEMO_IN_UP_CLICK, MEMO_STATE_LIST, MEMO_ACT_SELECT_PREV);
    expect(MEMO_STATE_LIST, MEMO_IN_DOWN_CLICK, MEMO_STATE_LIST, MEMO_ACT_SELECT_NEXT);
    expect(MEMO_STATE_LIST, MEMO_IN_OK_CLICK, MEMO_STATE_DETAIL, MEMO_ACT_OPEN_DETAIL);
    /* 逻辑待发从按下开始；短按不切页，准备动画使用一秒周期的剩余时间。 */
    expect(MEMO_STATE_LIST, MEMO_IN_OK_PRESS, MEMO_STATE_ARMING, MEMO_ACT_START_ARMING);
    expect_ignored(MEMO_STATE_LIST, MEMO_IN_OK_HOLD);
    expect(MEMO_STATE_LIST, MEMO_IN_UP_HOLD, MEMO_STATE_SETTINGS, MEMO_ACT_OPEN_SETTINGS);
    expect(MEMO_STATE_LIST, MEMO_IN_DOWN_HOLD, MEMO_STATE_SETTINGS, MEMO_ACT_OPEN_SETTINGS);
}

static void test_arming_fills_then_records(void)
{
    /* 圆环没填满就松手：不录音，回到列表。 */
    expect(MEMO_STATE_ARMING, MEMO_IN_OK_RELEASE, MEMO_STATE_LIST, MEMO_ACT_CANCEL_ARMING);
    /* 填满：开始录音，手指还按着。 */
    expect(MEMO_STATE_ARMING, MEMO_IN_ARM_COMPLETE, MEMO_STATE_RECORDING, MEMO_ACT_BEGIN_RECORDING);
    /* 待发阶段其它键不再有语义，避免误触。 */
    expect_ignored(MEMO_STATE_ARMING, MEMO_IN_UP_CLICK);
    expect_ignored(MEMO_STATE_ARMING, MEMO_IN_DOWN_CLICK);
}

static void test_recording_ends_on_release(void)
{
    /* 松手即结束，进入保存/取消；不再是"单击停止"。 */
    expect(MEMO_STATE_RECORDING, MEMO_IN_OK_RELEASE, MEMO_STATE_CONFIRM, MEMO_ACT_STOP_AND_SAVE);
    expect(MEMO_STATE_RECORDING, MEMO_IN_RECORD_LIMIT, MEMO_STATE_CONFIRM, MEMO_ACT_STOP_AND_SAVE);

    /* 录音中不响应导航，避免误触把一句话掐断。 */
    expect_ignored(MEMO_STATE_RECORDING, MEMO_IN_UP_CLICK);
    expect_ignored(MEMO_STATE_RECORDING, MEMO_IN_DOWN_CLICK);
    expect_ignored(MEMO_STATE_RECORDING, MEMO_IN_OK_CLICK);
    expect_ignored(MEMO_STATE_RECORDING, MEMO_IN_OK_HOLD);
}

static void test_confirm_sheet(void)
{
    /* 上/下切换高亮项 */
    expect(MEMO_STATE_CONFIRM, MEMO_IN_UP_CLICK, MEMO_STATE_CONFIRM, MEMO_ACT_CONFIRM_TOGGLE);
    expect(MEMO_STATE_CONFIRM, MEMO_IN_DOWN_CLICK, MEMO_STATE_CONFIRM, MEMO_ACT_CONFIRM_TOGGLE);
    /* 长按等同取消 */
    expect(MEMO_STATE_CONFIRM, MEMO_IN_OK_HOLD, MEMO_STATE_LIST, MEMO_ACT_CONFIRM_CANCEL);

    /* OK 的语义取决于当前高亮：默认"保存" */
    memo_sm_t sm;
    memo_sm_init(&sm);
    sm.state = MEMO_STATE_CONFIRM;
    sm.confirm_save = true;
    assert(memo_sm_handle(&sm, MEMO_IN_OK_CLICK) == MEMO_ACT_CONFIRM_SAVE);
    assert(sm.state == MEMO_STATE_LIST);

    /* 切到"取消"后再确认，返回的动作是取消 */
    sm.state = MEMO_STATE_CONFIRM;
    sm.confirm_save = true;
    assert(memo_sm_handle(&sm, MEMO_IN_UP_CLICK) == MEMO_ACT_CONFIRM_TOGGLE);
    assert(!sm.confirm_save);
    assert(memo_sm_handle(&sm, MEMO_IN_OK_CLICK) == MEMO_ACT_CONFIRM_CANCEL);
}

static void test_detail_cycle(void)
{
    expect(MEMO_STATE_DETAIL, MEMO_IN_OK_CLICK, MEMO_STATE_PLAYING, MEMO_ACT_PLAY);
    expect(MEMO_STATE_PLAYING, MEMO_IN_OK_CLICK, MEMO_STATE_PAUSED, MEMO_ACT_PAUSE);
    expect(MEMO_STATE_PAUSED, MEMO_IN_OK_CLICK, MEMO_STATE_PLAYING, MEMO_ACT_RESUME);
    for (int state=MEMO_STATE_DETAIL; state<=MEMO_STATE_PAUSED; state++) {
        expect(state, MEMO_IN_UP_CLICK, state, MEMO_ACT_VOLUME_UP);
        expect(state, MEMO_IN_DOWN_CLICK, state, MEMO_ACT_VOLUME_DOWN);
        expect(state, MEMO_IN_UP_HOLD, state, MEMO_ACT_DETAIL_PREV);
        expect(state, MEMO_IN_DOWN_HOLD, state, MEMO_ACT_DETAIL_NEXT);
        expect(state, MEMO_IN_OK_HOLD, MEMO_STATE_LIST, MEMO_ACT_BACK_TO_LIST);
    }
}

static void test_settings_and_subpage(void)
{
    expect(MEMO_STATE_SETTINGS, MEMO_IN_UP_CLICK, MEMO_STATE_SETTINGS, MEMO_ACT_SETTINGS_PREV);
    expect(MEMO_STATE_SETTINGS, MEMO_IN_DOWN_CLICK, MEMO_STATE_SETTINGS, MEMO_ACT_SETTINGS_NEXT);
    expect(MEMO_STATE_SETTINGS, MEMO_IN_OK_CLICK, MEMO_STATE_SUB_PAGE, MEMO_ACT_SUB_PAGE_ENTER);
    /* 进设置用长按上下键，离开也用上下键长按。 */
    expect(MEMO_STATE_SETTINGS, MEMO_IN_UP_HOLD, MEMO_STATE_LIST, MEMO_ACT_BACK_TO_LIST);
    expect(MEMO_STATE_SETTINGS, MEMO_IN_DOWN_HOLD, MEMO_STATE_LIST, MEMO_ACT_BACK_TO_LIST);
    expect_ignored(MEMO_STATE_SETTINGS, MEMO_IN_OK_HOLD);

    expect(MEMO_STATE_SUB_PAGE, MEMO_IN_OK_CLICK, MEMO_STATE_SETTINGS, MEMO_ACT_SUB_PAGE_SAVE);
    expect(MEMO_STATE_SUB_PAGE, MEMO_IN_OK_HOLD, MEMO_STATE_SETTINGS, MEMO_ACT_SUB_PAGE_CANCEL);
    expect(MEMO_STATE_SUB_PAGE, MEMO_IN_UP_CLICK, MEMO_STATE_SUB_PAGE, MEMO_ACT_SUB_PAGE_PREV);
}

static void test_ignored_inputs_are_explicit(void)
{
    /* 列表里短按之外的取消类输入不应有动作。 */
    expect_ignored(MEMO_STATE_LIST, MEMO_IN_OK_RELEASE);
    expect_ignored(MEMO_STATE_LIST, MEMO_IN_ARM_COMPLETE);
    expect_ignored(MEMO_STATE_LIST, MEMO_IN_RECORD_LIMIT);
    /* 详情里没有「开始录音」。 */
    expect_ignored(MEMO_STATE_DETAIL, MEMO_IN_ARM_COMPLETE);
}

static void test_screen_and_recording_flags(void)
{
    assert(memo_state_keeps_screen_on(MEMO_STATE_RECORDING));
    assert(memo_state_keeps_screen_on(MEMO_STATE_ARMING));
    assert(memo_state_keeps_screen_on(MEMO_STATE_PLAYING));
    assert(!memo_state_keeps_screen_on(MEMO_STATE_LIST));
    assert(!memo_state_keeps_screen_on(MEMO_STATE_PAUSED));

    assert(memo_state_is_recording(MEMO_STATE_RECORDING));
    assert(!memo_state_is_recording(MEMO_STATE_ARMING));
}

static void test_every_state_handles_every_input(void)
{
    /* 没有崩溃、没有越界，且 ACTION/state 名字都能取到。 */
    for (int s = 0; s < MEMO_STATE_COUNT; s++) {
        for (int i = 0; i < MEMO_IN_COUNT; i++) {
            memo_sm_t sm = {.state = (memo_state_t)s};
            memo_action_t a = memo_sm_handle(&sm, (memo_input_t)i);
            assert(a >= MEMO_ACT_NONE && a < MEMO_ACT_COUNT);
            assert(sm.state >= 0 && sm.state < MEMO_STATE_COUNT);
            assert(strcmp(memo_state_name(sm.state), "?") != 0);
            assert(strcmp(memo_action_name(a), "?") != 0);
        }
    }
}

static void test_rejection_choice(void)
{
    memo_sm_t sm;
    memo_sm_init(&sm);
    assert(!memo_sm_open_rejection(NULL));
    for (int state = 0; state < MEMO_STATE_COUNT; state++) {
        sm.state = (memo_state_t)state;
        assert(memo_sm_open_rejection(&sm) ==
               (state == MEMO_STATE_LIST || state == MEMO_STATE_UPLOAD_REJECTED));
    }
    sm.state = MEMO_STATE_LIST;
    assert(memo_sm_open_rejection(&sm));
    assert(!sm.reject_delete);
    assert(memo_sm_handle(&sm, MEMO_IN_OK_CLICK) == MEMO_ACT_REJECT_RETRY);
    assert(sm.state == MEMO_STATE_UPLOAD_REJECTED); /* wait for worker ack */
    assert(memo_sm_handle(&sm, MEMO_IN_DOWN_CLICK) == MEMO_ACT_REJECT_TOGGLE);
    assert(sm.reject_delete);
    assert(memo_sm_handle(&sm, MEMO_IN_OK_CLICK) == MEMO_ACT_REJECT_DELETE);
    assert(memo_sm_open_rejection(&sm)); /* another file resets destructive selection */
    assert(!sm.reject_delete);
    memo_sm_handle(&sm, MEMO_IN_DOWN_CLICK);
    assert(memo_sm_handle(&sm, MEMO_IN_OK_HOLD) == MEMO_ACT_REJECT_RETRY);
    assert(!sm.reject_delete);
    expect_ignored(MEMO_STATE_UPLOAD_REJECTED, MEMO_IN_OK_PRESS);
    expect_ignored(MEMO_STATE_UPLOAD_REJECTED, MEMO_IN_OK_RELEASE);
    expect_ignored(MEMO_STATE_UPLOAD_REJECTED, MEMO_IN_UP_HOLD);
    assert(memo_state_keeps_screen_on(MEMO_STATE_UPLOAD_REJECTED));
}

int main(void)
{
    test_list_navigation();
    test_arming_fills_then_records();
    test_recording_ends_on_release();
    test_confirm_sheet();
    expect(MEMO_STATE_AUTOSAVE, MEMO_IN_OK_CLICK, MEMO_STATE_LIST, MEMO_ACT_CONFIRM_CANCEL);
    expect_ignored(MEMO_STATE_AUTOSAVE, MEMO_IN_OK_HOLD);
    assert(memo_state_keeps_screen_on(MEMO_STATE_AUTOSAVE));
    test_detail_cycle();
    test_settings_and_subpage();
    test_ignored_inputs_are_explicit();
    test_screen_and_recording_flags();
    test_every_state_handles_every_input();
    test_rejection_choice();
    puts("memo state machine: PASS");
    return 0;
}
