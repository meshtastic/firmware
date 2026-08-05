"""FN_F1..F5 directly jumps to frame 0..4 via Screen::handleInputEvent.

Parametrized over the 5 function keys. Each expects a
`Screen: frame <idx>/<count> name=... reason=fn_f<k>` log line, with
`idx == k-1`. We don't hardcode the frame *name* because the layout
depends on which modules are compiled in for this board.
"""

from __future__ import annotations

import time

import pytest
from meshtastic_mcp.input_events import InputEventCode

from ._screen_log import get_current_frame, wait_for_reason
from .conftest import FrameCapture, send_event


@pytest.mark.timeout(120)
@pytest.mark.parametrize(
    "event,expected_idx,reason",
    [
        (InputEventCode.FN_F1, 0, "fn_f1"),
        (InputEventCode.FN_F2, 1, "fn_f2"),
        (InputEventCode.FN_F3, 2, "fn_f3"),
        (InputEventCode.FN_F4, 3, "fn_f4"),
        (InputEventCode.FN_F5, 4, "fn_f5"),
    ],
    ids=["FN_F1", "FN_F2", "FN_F3", "FN_F4", "FN_F5"],
)
def test_fn_jump_direct_frame(
    ui_port: str,
    frame_capture: FrameCapture,
    request: pytest.FixtureRequest,
    event: InputEventCode,
    expected_idx: int,
    reason: str,
) -> None:
    lines: list[str] = request.node._debug_log_buffer
    start = get_current_frame(lines)
    assert start is not None, "no frame log yet — USERPREFS_UI_TEST_LOG not wired?"
    assert start.name in (
        "home",
        "deviceFocused",
    ), f"setup expected frame 0 landing, got {start.name!r}"
    frame_capture("initial")

    if start.count <= expected_idx:
        pytest.skip(
            f"device has {start.count} frames; FN_F{expected_idx + 1} needs > {expected_idx}"
        )

    send_event(ui_port, event)
    time.sleep(0.1)
    evt = wait_for_reason(lines, reason, timeout_s=5.0)
    assert evt.idx == expected_idx, (
        f"FN_F{expected_idx + 1} expected idx={expected_idx}, got {evt.idx} "
        f"(name={evt.name}, count={evt.count})"
    )
    frame_capture(f"after-{reason}")
