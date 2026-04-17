"""Telemetry placeholders: v2 roadmap entries, skipped in v2.0."""

from __future__ import annotations

import pytest

pytestmark = pytest.mark.skip(reason="placeholder — implement in v2.1")


def test_position_broadcast_precision() -> None:
    """Bake A with `USERPREFS_CHANNEL_0_PRECISION=10` (low-precision channel);
    A sets a fixed position; B's `list_nodes` entry for A shows truncated
    coordinates (lower bits zeroed relative to what A transmitted). Validates
    the privacy-precision knob operators tune for event deployments."""
    pass


def test_fixed_position_round_trip() -> None:
    """`set_fixed_position(lat=40.7128, lon=-74.0060, alt=10)` on A; wait for
    broadcast; assert B's node DB shows A's position within 1e-6° tolerance
    AND `position_config.fixed_position=true` on A persists across reboot."""
    pass
