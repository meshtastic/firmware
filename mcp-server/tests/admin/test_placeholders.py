"""Admin placeholders: v2 roadmap entries, skipped in v2.0."""

from __future__ import annotations

import pytest

pytestmark = pytest.mark.skip(reason="placeholder — implement in v2.1")


def test_set_channel_url_imports_all() -> None:
    """Synthesize a URL with primary + 2 secondary channels; `set_channel_url`;
    verify all 3 show up in `localNode.channels` with correct PSKs + names.
    Common operator flow: deploy a new channel scheme across a fleet."""
    pass


def test_telemetry_interval_configurable() -> None:
    """`set_config("telemetry.device_update_interval", 60)`; wait 2× interval;
    verify telemetry packets arrived at ~60s cadence (within ±20% jitter
    tolerance). A frequent 'my telemetry isn't working' user issue."""
    pass


def test_fixed_position_set_and_reported() -> None:
    """`set_fixed_position(lat, lon, alt)`; verify `position_config.fixed_position=true`
    and `list_nodes()[self].position` reports lat/lon within 1e-6 degree tolerance.
    The privacy-vs-utility trade-off position-precision users care about."""
    pass
