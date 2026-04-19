"""SELECT on the home frame opens the home menu; BACK closes it.

The home menu is an overlay (menuHandler::homeBaseMenu), not a frame
transition — so we verify via OCR difference between before/after
captures rather than a `Screen: frame` log line. The underlying
mechanism is still InputBroker → Screen::handleInputEvent → menu
callback.
"""

from __future__ import annotations

import time

import pytest
from meshtastic_mcp.input_events import InputEventCode

from ._screen_log import get_current_frame
from .conftest import FrameCapture, send_event


@pytest.mark.timeout(120)
def test_select_opens_home_menu(
    ui_port: str,
    frame_capture: FrameCapture,
    request: pytest.FixtureRequest,
) -> None:
    lines: list[str] = request.node._debug_log_buffer
    start = get_current_frame(lines)
    assert start is not None
    if start.name not in ("home", "deviceFocused"):
        pytest.skip(
            f"SELECT on {start.name!r} doesn't open homeBaseMenu; "
            "test is only valid when the landing frame is home/deviceFocused"
        )

    initial = frame_capture("initial")
    send_event(ui_port, InputEventCode.SELECT)
    time.sleep(0.8)
    opened = frame_capture("after-select")

    # The menu is an overlay (not a frame change). We cannot use log
    # assertion — instead, OCR should differ because a menu list is now
    # drawn on top.
    initial_text = (initial.get("ocr_text") or "").strip()
    opened_text = (opened.get("ocr_text") or "").strip()
    if initial_text and opened_text:
        # When OCR is available, require *some* difference between the two
        # frames — even a single menu title changes the transcribed text.
        assert initial_text != opened_text, (
            f"expected OCR diff after SELECT; both read {initial_text!r}. "
            "If both are empty, check camera alignment + OCR backend."
        )

    # Back out — the menu dismisses on BACK.
    send_event(ui_port, InputEventCode.BACK)
    time.sleep(0.8)
    closed = frame_capture("after-back")

    # Soft check: OCR after BACK should look different from the menu
    # (either back to home or onto a previous frame — BACK's exact
    # behavior when the menu is up vs. not-up varies). We don't assert
    # equality because OLED rendering is pixel-stable but camera sampling
    # introduces noise.
    if opened_text and closed.get("ocr_text"):
        close_text = (closed.get("ocr_text") or "").strip()
        assert (
            close_text != opened_text
        ), f"after BACK, OCR still looks like the menu: {close_text!r}"
