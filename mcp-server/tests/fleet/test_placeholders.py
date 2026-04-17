"""Fleet placeholders: v2 roadmap entries, skipped in v2.0."""

from __future__ import annotations

import pytest

pytestmark = pytest.mark.skip(reason="placeholder — implement in v2.1")


def test_reflash_is_idempotent() -> None:
    """`erase_and_flash` + wait-for-boot; `erase_and_flash` again with same
    env+profile; second pass completes with the device reachable and
    `device_info` identical to first pass. Operational CI workflow check."""
    pass


def test_concurrent_admin_ops_dont_stomp() -> None:
    """Launch two threads: one calls `device_info(nrf52)` repeatedly, the other
    calls `device_info(esp32s3)`. Both succeed, no cross-port contamination.
    Validates `connection.py`'s per-port lock."""
    pass
