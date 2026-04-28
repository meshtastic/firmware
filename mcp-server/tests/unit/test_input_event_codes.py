"""Pin `InputEventCode` values to the firmware `input_broker_event` enum.

If this test fails, someone changed the firmware enum (or this Python
mirror) and they must stay in sync — the admin RPC sends these as u8
wire values directly.

Also exercises `coerce_event_code` for the happy + error paths.
"""

from __future__ import annotations

import pytest
from meshtastic_mcp.input_events import InputEventCode, coerce_event_code


class TestInputEventCodeValues:
    """These values MUST match src/input/InputBroker.h exactly."""

    def test_navigation_keys(self) -> None:
        assert int(InputEventCode.UP) == 17
        assert int(InputEventCode.DOWN) == 18
        assert int(InputEventCode.LEFT) == 19
        assert int(InputEventCode.RIGHT) == 20

    def test_action_keys(self) -> None:
        assert int(InputEventCode.SELECT) == 10
        assert int(InputEventCode.CANCEL) == 24
        assert int(InputEventCode.BACK) == 27

    def test_long_press_variants(self) -> None:
        assert int(InputEventCode.SELECT_LONG) == 11
        assert int(InputEventCode.UP_LONG) == 12
        assert int(InputEventCode.DOWN_LONG) == 13

    def test_fn_keys(self) -> None:
        assert int(InputEventCode.FN_F1) == 0xF1
        assert int(InputEventCode.FN_F2) == 0xF2
        assert int(InputEventCode.FN_F3) == 0xF3
        assert int(InputEventCode.FN_F4) == 0xF4
        assert int(InputEventCode.FN_F5) == 0xF5

    def test_system_events(self) -> None:
        assert int(InputEventCode.SHUTDOWN) == 0x9B
        assert int(InputEventCode.GPS_TOGGLE) == 0x9E
        assert int(InputEventCode.SEND_PING) == 0xAF

    def test_auto_increment_block(self) -> None:
        # C enum: `BACK = 27, USER_PRESS, ALT_PRESS, ALT_LONG` → 28, 29, 30.
        assert int(InputEventCode.USER_PRESS) == 28
        assert int(InputEventCode.ALT_PRESS) == 29
        assert int(InputEventCode.ALT_LONG) == 30


class TestCoerceEventCode:
    def test_int_passthrough(self) -> None:
        assert coerce_event_code(20) == 20
        assert coerce_event_code(0) == 0
        assert coerce_event_code(255) == 255

    def test_enum_passthrough(self) -> None:
        assert coerce_event_code(InputEventCode.RIGHT) == 20
        assert coerce_event_code(InputEventCode.FN_F1) == 0xF1

    def test_name_case_insensitive(self) -> None:
        assert coerce_event_code("right") == 20
        assert coerce_event_code("RIGHT") == 20
        assert coerce_event_code("Right") == 20

    def test_input_broker_prefix_stripped(self) -> None:
        assert coerce_event_code("INPUT_BROKER_FN_F1") == 0xF1
        assert coerce_event_code("input_broker_select") == 10

    def test_hyphen_and_underscore_equivalence(self) -> None:
        assert coerce_event_code("fn-f1") == 0xF1

    def test_int_out_of_range_raises(self) -> None:
        with pytest.raises(ValueError, match="u8"):
            coerce_event_code(256)
        with pytest.raises(ValueError, match="u8"):
            coerce_event_code(-1)

    def test_unknown_name_raises(self) -> None:
        with pytest.raises(ValueError, match="unknown event code name"):
            coerce_event_code("NOT_A_KEY")

    def test_wrong_type_raises(self) -> None:
        with pytest.raises(TypeError):
            coerce_event_code(1.5)  # type: ignore[arg-type]
        with pytest.raises(TypeError):
            coerce_event_code(None)  # type: ignore[arg-type]
