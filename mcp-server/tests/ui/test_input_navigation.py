"""INPUT_BROKER_RIGHT cycles forward through frames; INPUT_BROKER_LEFT backs.

The simplest UI test: fire N RIGHT events and assert the frame index
moves forward by N (modulo frameCount). Each step captures an image +
OCR for the HTML report.
"""

from __future__ import annotations

import time
from typing import Any

import pytest
from meshtastic_mcp.input_events import InputEventCode

from ._screen_log import get_current_frame, wait_for_frame
from .conftest import FrameCapture, send_event


@pytest.mark.timeout(120)
def test_input_right_cycles_frames(
    ui_port: str,
    frame_capture: FrameCapture,
    request: pytest.FixtureRequest,
) -> None:
    lines: list[str] = request.node._debug_log_buffer
    start = get_current_frame(lines)
    assert start is not None, "no frame log yet — USERPREFS_UI_TEST_LOG not wired?"
    # FN_F1 in ui_home_state lands on frame 0. The name at frame 0 varies
    # by board (home on heltec-v3, deviceFocused on others) — accept either.
    assert start.name in (
        "home",
        "deviceFocused",
    ), f"setup expected home/deviceFocused at frame 0, got {start.name!r}"

    frame_capture("initial")
    visited = [start.idx]

    for step in range(4):
        send_event(ui_port, InputEventCode.RIGHT)
        # Each RIGHT should bump the frame index by 1. The log fires with
        # `reason=next` from showFrame(NEXT).
        before_count = len(list(_frame_events(lines)))
        deadline = time.monotonic() + 5.0
        while time.monotonic() < deadline:
            if len(list(_frame_events(lines))) > before_count:
                break
            time.sleep(0.1)
        evt = get_current_frame(lines)
        assert evt is not None
        assert (
            evt.reason == "next"
        ), f"step {step}: expected reason=next, got {evt.reason!r}"
        visited.append(evt.idx)
        frame_capture(f"after-right-{step + 1}")

    # Sanity: each index should differ from its predecessor.
    diffs = [visited[i + 1] - visited[i] for i in range(len(visited) - 1)]
    assert all(
        d in (1, -(start.count - 1)) for d in diffs
    ), f"expected monotonic +1 steps (or a wrap), got visited={visited} diffs={diffs}"


@pytest.mark.timeout(120)
def test_input_left_returns_to_home(
    ui_port: str,
    frame_capture: FrameCapture,
    request: pytest.FixtureRequest,
) -> None:
    """After RIGHT×3 + LEFT×3, we should end up back on the starting frame."""
    lines: list[str] = request.node._debug_log_buffer
    start = get_current_frame(lines)
    assert start is not None
    start_name = start.name
    frame_capture("initial")
    for _ in range(3):
        send_event(ui_port, InputEventCode.RIGHT)
        time.sleep(0.3)
    frame_capture("after-right-3")

    for _ in range(3):
        send_event(ui_port, InputEventCode.LEFT)
        time.sleep(0.3)

    # Back to whichever frame we started on (home or deviceFocused).
    wait_for_frame(lines, start_name, timeout_s=5.0)
    frame_capture(f"after-left-3-back-{start_name}")


def _frame_events(lines: list[str]) -> Any:
    from ._screen_log import iter_frame_events

    return iter_frame_events(lines)
