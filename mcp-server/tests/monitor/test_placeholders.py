"""Monitor placeholders: v2 roadmap entries, skipped in v2.0."""

from __future__ import annotations

import pytest

pytestmark = pytest.mark.skip(reason="placeholder — implement in v2.1")


def test_env_selects_correct_filter() -> None:
    """Open `serial_open(env='t-beam-1w')`; assert `resolved_filters` includes
    `esp32_exception_decoder`. Proves `pio device monitor -e <env>` correctly
    applies per-variant monitor_filters from platformio.ini."""
    pass


def test_dropped_lines_reported() -> None:
    """Force a flood of log output (e.g. via a debug flag); read the session
    with a long delay; assert `serial_read.dropped > 0` surfaces correctly
    when the ring buffer overflows. Operators relying on serial capture for
    post-mortem need to know when they missed data."""
    pass
