"""Provisioning: after a non-full factory_reset, USERPREFS defaults come back.

Real operator concern: "if someone resets my fleet device, will it come back
on my private mesh or on Meshtastic defaults?" A baked USERPREFS recipe
should be the factory floor for the device — reset goes back to THAT state,
not to stock Meshtastic.
"""

from __future__ import annotations

import time
from typing import Any

import pytest
from meshtastic_mcp import admin, info


@pytest.mark.timeout(120)
def test_baked_prefs_survive_factory_reset(
    baked_mesh: dict[str, Any],
    test_profile: dict[str, Any],
    wait_until,
) -> None:
    """Flow:
    1. Change owner name to a known-non-default value.
    2. Trigger factory_reset(full=False).
    3. Wait for device to come back.
    4. Confirm owner is back to USERPREFS-baked default (or blank default if
       not baked), and primary channel/region/slot are still the baked values.
    """
    # Use esp32s3 — typically more robust across reset cycles.
    target = "esp32s3"
    if target not in baked_mesh:
        pytest.skip(f"role {target!r} not on hub")
    port = baked_mesh[target]["port"]

    # Snapshot pre-reset config
    pre_reset = info.device_info(port=port, timeout_s=8.0)
    original_long_name = pre_reset.get("long_name")

    # Poison the owner name with a non-default marker
    admin.set_owner(long_name="PoisonMarker", short_name="POIZ", port=port)
    time.sleep(2.0)

    # Confirm poison stuck before reset
    poisoned = info.device_info(port=port, timeout_s=8.0)
    assert poisoned.get("long_name") == "PoisonMarker"

    # Trigger non-full factory reset
    admin.factory_reset(port=port, confirm=True, full=False)

    # Wait for device to come back (serial reappears)
    time.sleep(10.0)  # reset takes a moment
    wait_until(
        lambda: info.device_info(port=port, timeout_s=5.0).get("my_node_num")
        is not None,
        timeout=60,
        backoff_start=1.0,
    )

    post = info.device_info(port=port, timeout_s=8.0)
    # The key assertion: channel + region are STILL the USERPREFS-baked values,
    # NOT Meshtastic stock defaults (which would be "LongFast" and the region
    # the device shipped with).
    assert post["primary_channel"] == test_profile["USERPREFS_CHANNEL_0_NAME"], (
        f"after factory_reset, primary_channel reverted to "
        f"{post['primary_channel']!r}, not baked {test_profile['USERPREFS_CHANNEL_0_NAME']!r}"
    )
    expected_region = test_profile["USERPREFS_CONFIG_LORA_REGION"].rsplit("_", 1)[-1]
    assert post.get("region") == expected_region

    # Owner name should NOT be "PoisonMarker" anymore
    assert (
        post.get("long_name") != "PoisonMarker"
    ), "factory_reset did not wipe the poisoned owner name"

    # If we had an original_long_name, restore it so downstream tests see
    # the same baseline.
    if original_long_name and post.get("long_name") != original_long_name:
        admin.set_owner(long_name=original_long_name, port=port)
