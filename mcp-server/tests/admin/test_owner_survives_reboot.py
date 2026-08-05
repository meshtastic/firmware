"""Admin: owner name persists across a reboot.

The single most common "did my admin change stick?" test. Proves
`localNode.setOwner()` + `writeConfig("device")` commits to non-volatile
storage before the reboot.
"""

from __future__ import annotations

import time
from typing import Any

import pytest
from meshtastic_mcp import admin, info


@pytest.mark.timeout(120)
def test_owner_survives_reboot(
    baked_single: dict[str, Any],
    wait_until,
) -> None:
    """Runs once per connected role — proves the reboot-persistence
    round-trip works on each device independently, not just one."""
    port = baked_single["port"]

    pre = info.device_info(port=port, timeout_s=8.0)
    original = pre.get("long_name") or ""
    marker = "RebootSurvive"
    try:
        admin.set_owner(long_name=marker, short_name="RS", port=port)
        time.sleep(1.5)

        # Confirm pre-reboot
        confirmed = info.device_info(port=port, timeout_s=8.0)
        assert confirmed["long_name"] == marker

        # Reboot (short delay)
        admin.reboot(port=port, confirm=True, seconds=3)

        # Wait for device to come back
        time.sleep(8.0)
        wait_until(
            lambda: info.device_info(port=port, timeout_s=5.0).get("my_node_num")
            is not None,
            timeout=60,
            backoff_start=1.0,
        )

        post = info.device_info(port=port, timeout_s=8.0)
        assert post["long_name"] == marker, (
            f"owner name did not persist across reboot: "
            f"expected {marker!r}, got {post['long_name']!r}"
        )
    finally:
        # Restore original (best-effort)
        try:
            admin.set_owner(long_name=original or "TestNode", port=port)
        except Exception:
            pass
