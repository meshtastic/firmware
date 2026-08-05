"""Smoke test: `uhubctl_list` returns a well-formed structure.

No destructive action. Runs first in the tier as a sanity check that the
tier's dependencies (uhubctl binary + permissions) are actually satisfied.
"""

from __future__ import annotations

import pytest
from meshtastic_mcp import uhubctl


@pytest.mark.timeout(30)
def test_list_hubs_returns_at_least_one_ppps_hub() -> None:
    hubs = uhubctl.list_hubs()
    assert hubs, "uhubctl found no hubs at all — is a USB hub connected?"
    assert any(h["ppps"] for h in hubs), (
        "no PPPS-capable hubs detected; power control won't work. "
        "Check that the hub supports Per-Port Power Switching."
    )


@pytest.mark.timeout(30)
def test_list_hubs_structure(hub_devices: dict[str, str]) -> None:
    hubs = uhubctl.list_hubs()
    for hub in hubs:
        assert "location" in hub and hub["location"]
        assert "ports" in hub and isinstance(hub["ports"], list)
        for port in hub["ports"]:
            assert "port" in port and isinstance(port["port"], int)
            assert "status" in port

    # At least one of the detected Meshtastic roles should show up in some
    # port's device_vid — otherwise the recovery tier can't drive them.
    seen_vids = {
        p["device_vid"] for h in hubs for p in h["ports"] if p["device_vid"] is not None
    }
    expected_any = {0x239A, 0x303A, 0x10C4} & seen_vids
    assert expected_any or not hub_devices, (
        f"hub_devices detected roles {list(hub_devices)} but uhubctl sees "
        f"VIDs {sorted(hex(v) for v in seen_vids)} — the devices may be on "
        "a hub that uhubctl can't see (e.g. built-in laptop ports)."
    )
