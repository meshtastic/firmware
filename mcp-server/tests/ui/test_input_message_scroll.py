"""Once we navigate to the textMessage frame, UP/DOWN exercises the
message-scroll path (or opens CannedMessages on empty devices).

Weaker than a "no frame change" assertion because on a fresh bench
device the message store is usually empty, and the firmware's UP
handler in that case launches CannedMessage — which DOES rebuild
frames. We just verify the path doesn't crash + produce captures for
visual inspection.
"""

from __future__ import annotations

import time

import pytest
from meshtastic_mcp.input_events import InputEventCode

from ._screen_log import get_current_frame, wait_for_frame
from .conftest import FrameCapture, send_event


@pytest.mark.timeout(180)
def test_up_down_on_textmessage_survives(
    ui_port: str,
    frame_capture: FrameCapture,
    request: pytest.FixtureRequest,
) -> None:
    lines: list[str] = request.node._debug_log_buffer
    frame_capture("initial")

    # Walk RIGHT until we land on textMessage — up to 15 hops.
    for _i in range(15):
        send_event(ui_port, InputEventCode.RIGHT)
        time.sleep(0.3)
        current = get_current_frame(lines)
        if current is not None and current.name == "textMessage":
            break
    else:
        pytest.skip(
            "couldn't reach textMessage frame within 15 RIGHTs — not present on this board"
        )

    wait_for_frame(lines, "textMessage", timeout_s=5.0)
    frame_capture("on-textMessage")

    # UP and DOWN exercise the message-scroll / canned-message-launch path.
    # Capture after each so the HTML report shows any visual effect.
    send_event(ui_port, InputEventCode.UP)
    time.sleep(0.3)
    frame_capture("after-up")

    send_event(ui_port, InputEventCode.DOWN)
    time.sleep(0.3)
    frame_capture("after-down")

    # Soft check: we should still be in a reachable frame (not wedged).
    # The next test's `ui_home_state` will error out if the device is
    # unresponsive, so we don't need a stricter guarantee here.
    final = get_current_frame(lines)
    assert final is not None, "no frame log after UP/DOWN — event path broke"
