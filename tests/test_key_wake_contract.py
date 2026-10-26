#!/usr/bin/env python3
"""Static contract for the deep-sleep key-wake guard wiring.

The guard exists because a key wake reboots the chip while that key is still
held: the button component starts a few hundred milliseconds later, sees a
press that is already down, and grades it a long press almost immediately.

The contract is ordering, not just presence: :func:`on_key` must consume the
guard *before* it enqueues anything, otherwise a release event arriving before
the input task is ready would leave the guard armed.
"""

from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
APP = ROOT / "main" / "memo_app.c"


def function_body(source: str, name: str) -> str:
    match = re.search(rf"\b{re.escape(name)}\s*\([^;]*?\)\s*\{{", source)
    if not match:
        raise AssertionError(f"function not found: {name}")
    start = match.end() - 1
    depth = 0
    for index in range(start, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start + 1:index]
    raise AssertionError(f"function is unterminated: {name}")


class KeyWakeGuardWiringTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.source = APP.read_text(encoding="utf-8")

    def test_on_key_consumes_the_guard_before_dispatching(self) -> None:
        body = function_body(self.source, "on_key")
        self.assertIn("key_guard_consume(", body)
        consume = body.index("key_guard_consume(")
        for later in ("enqueue_input(", "xQueueSend("):
            if later in body:
                self.assertLess(consume, body.index(later),
                                f"{later} must be dispatched after the guard check")

    def test_wake_arms_the_guard(self) -> None:
        self.assertIn("ESP_SLEEP_WAKEUP_GPIO", self.source)
        self.assertIn("key_guard_arm(", self.source)

    def test_init_samples_the_physical_key_after_button_start(self) -> None:
        # The component never reports a release for a key that was let go before
        # it started observing, so the guard must be disarmed from the measured
        # voltage as well.
        self.assertIn("key_guard_sync_released(", self.source)
        self.assertIn("bsp_button_read_mv()", self.source)

    def test_button_callback_stays_non_blocking(self) -> None:
        body = function_body(self.source, "on_key")
        for forbidden in ("lv_", "bsp_audio_", "memo_recorder_", "fopen("):
            self.assertNotIn(forbidden, body,
                             f"on_key runs on the esp_timer task and must not call {forbidden}")


if __name__ == "__main__":
    unittest.main()
