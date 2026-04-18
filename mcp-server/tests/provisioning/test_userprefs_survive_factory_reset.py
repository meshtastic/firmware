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

from .._port_discovery import resolve_port_by_role


@pytest.mark.timeout(180)
def test_baked_prefs_survive_factory_reset(
    baked_single: dict[str, Any],
    test_profile: dict[str, Any],
    wait_until,
) -> None:
    """Runs once per connected role. Flow:
    1. Change owner name to a known-non-default value.
    2. Trigger factory_reset(full=False).
    3. Rediscover the port (macOS re-enumerates the CDC endpoint on nRF52
       factory_reset; the path can change e.g. `/dev/cu.usbmodem101` →
       `/dev/cu.usbmodem1101`).
    4. Wait for device to come back.
    5. Confirm owner is back to USERPREFS-baked default (or blank default if
       not baked), and primary channel/region/slot are still the baked values.
    """
    role = baked_single["role"]
    port = baked_single["port"]

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

    # Device re-enumerates — rediscover its port before probing. nRF52's
    # CDC endpoint drops and comes back with a new `/dev/cu.usbmodem*`
    # path on macOS; ESP32-S3 usually keeps the same path but the helper
    # works either way (it just returns the current path for this role).
    # Early sleep lets the USB kernel driver settle before we start
    # polling — list_devices during a transient re-enumeration can return
    # an empty list and the helper's poll-with-backoff handles that too,
    # so the sleep is optimization not correctness.
    time.sleep(10.0)
    port = resolve_port_by_role(role, timeout_s=60.0)
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
