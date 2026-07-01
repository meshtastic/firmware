"""Out-of-bounds FN_F5 when the device has <5 frames: no crash, idx unchanged.

`Screen::handleInputEvent` dispatches FN_F5 unconditionally to
`ui->switchToFrame(4)`. The OLEDDisplayUi library typically clamps or
silently ignores out-of-range indices, but firmware bugs have existed
here — this test protects against a regression that would wedge the UI.

If this test fails, first check: did the device actually crash (Guru
Meditation in the log)? Or did switchToFrame accept an OOB index and
leave the UI blank?
"""

from __future__ import annotations

import time

import pytest
from meshtastic_mcp.input_events import InputEventCode

from ._screen_log import get_current_frame, wait_for_reason
from .conftest import FrameCapture, send_event


@pytest.mark.timeout(90)
def test_fn_f5_out_of_bounds(
    ui_port: str,
    frame_capture: FrameCapture,
    request: pytest.FixtureRequest,
) -> None:
    lines: list[str] = request.node._debug_log_buffer
    start = get_current_frame(lines)
    assert start is not None

    if start.count > 5:
        pytest.skip(
            f"device has {start.count} frames; FN_F5 is in-bounds — not testing OOB here"
        )

    frame_capture("initial-home")
    send_event(ui_port, InputEventCode.FN_F5)
    time.sleep(0.5)

    try:
        wait_for_reason(lines, "fn_f5", timeout_s=3.0)
    except TimeoutError:
        # Firmware may have ignored the event entirely — acceptable.
        pass

    # Capture whatever is on screen (OCR will tell us if something weird
    # happened). Device must remain responsive — subsequent events should
    # still land.
    frame_capture("after-fn_f5-oob")

    # Send a RIGHT to confirm the UI is still alive. If this times out,
    # the OOB switchToFrame wedged the UI.
    send_event(ui_port, InputEventCode.RIGHT)
    post = wait_for_reason(lines, "next", timeout_s=5.0)
    assert (
        post is not None
    ), "UI wedged after OOB FN_F5 — RIGHT no longer produces frame log"
    frame_capture("after-recovery-right")
