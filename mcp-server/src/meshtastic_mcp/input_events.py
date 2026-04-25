"""Python mirror of firmware `enum input_broker_event` (src/input/InputBroker.h).

Used by `admin.send_input_event` + `tests/ui/` so callers can say
`InputEventCode.RIGHT` instead of hard-coding 20. Values MUST stay in sync
with the firmware enum — unit test `tests/unit/test_input_event_codes.py`
pins the mapping.
"""

from __future__ import annotations

from enum import IntEnum


class InputEventCode(IntEnum):
    """Button / key / gesture events dispatched by the firmware InputBroker."""

    NONE = 0
    SELECT = 10
    SELECT_LONG = 11
    UP_LONG = 12
    DOWN_LONG = 13
    UP = 17
    DOWN = 18
    LEFT = 19
    RIGHT = 20
    CANCEL = 24
    BACK = 27
    # Auto-incremented values in the C enum (27 + 1, +2, +3):
    USER_PRESS = 28
    ALT_PRESS = 29
    ALT_LONG = 30
    SHUTDOWN = 0x9B
    GPS_TOGGLE = 0x9E
    SEND_PING = 0xAF
    FN_F1 = 0xF1
    FN_F2 = 0xF2
    FN_F3 = 0xF3
    FN_F4 = 0xF4
    FN_F5 = 0xF5
    MATRIXKEY = 0xFE
    ANYKEY = 0xFF


def coerce_event_code(value: int | str | InputEventCode) -> int:
    """Accept an int, a case-insensitive name, or an `InputEventCode` and return
    the u8 wire value. Raises ValueError on unknown names / out-of-range ints.
    """
    if isinstance(value, InputEventCode):
        return int(value)
    if isinstance(value, int):
        if not 0 <= value <= 255:
            raise ValueError(f"event_code out of u8 range: {value}")
        return value
    if isinstance(value, str):
        key = value.upper().replace("-", "_")
        if key.startswith("INPUT_BROKER_"):
            key = key[len("INPUT_BROKER_") :]
        try:
            return int(InputEventCode[key])
        except KeyError as exc:
            known = ", ".join(m.name for m in InputEventCode)
            raise ValueError(
                f"unknown event code name {value!r}; known: {known}"
            ) from exc
    raise TypeError(
        f"event_code must be int|str|InputEventCode, got {type(value).__name__}"
    )
