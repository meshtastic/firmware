"""On the nodelist_nodes frame, UP/DOWN scrolls the list via
`NodeListRenderer::scrollUp/scrollDown` (src/graphics/Screen.cpp:1779-1788).
The firmware returns 0 before notifying observers, so no frame-change
log fires. Verify the path doesn't crash and we stay on nodelist_nodes.
"""

from __future__ import annotations

import time

import pytest
from meshtastic_mcp.input_events import InputEventCode

from ._screen_log import assert_no_frame_change, get_current_frame, wait_for_frame
from .conftest import FrameCapture, send_event


@pytest.mark.timeout(180)
def test_up_down_on_nodelist_no_frame_change(
    ui_port: str,
    frame_capture: FrameCapture,
    request: pytest.FixtureRequest,
) -> None:
    lines: list[str] = request.node._debug_log_buffer
    frame_capture("initial")

    # Walk RIGHT until we land on nodelist_nodes.
    for _i in range(15):
        send_event(ui_port, InputEventCode.RIGHT)
        time.sleep(0.3)
        current = get_current_frame(lines)
        if current is not None and current.name == "nodelist_nodes":
            break
    else:
        pytest.skip("couldn't reach nodelist_nodes within 15 RIGHTs")

    wait_for_frame(lines, "nodelist_nodes", timeout_s=5.0)
    frame_capture("on-nodelist")

    # UP/DOWN on nodelist scroll internally + `return 0` before
    # notifyObservers — no frame-change log. Verify.
    send_event(ui_port, InputEventCode.UP)
    assert_no_frame_change(lines, wait_s=1.5)
    send_event(ui_port, InputEventCode.DOWN)
    assert_no_frame_change(lines, wait_s=1.5)

    final = get_current_frame(lines)
    assert (
        final is not None and final.name == "nodelist_nodes"
    ), f"UP/DOWN moved us off nodelist_nodes; now on {final!r}"
    frame_capture("after-up-down")
