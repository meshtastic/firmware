"""Provisioning: the pre-bake recipe (US/LONG_FAST/slot 88/private channel)
lands on the device exactly as specified.

This is THE test that proves the MCP's core value prop — flashing firmware
with a preset USERPREFS produces a device in the expected radio config without
any post-flash admin steps.
"""

from __future__ import annotations

from typing import Any

import pytest
from meshtastic_mcp import admin, info


@pytest.mark.timeout(60)
def test_bake_sets_region_preset_and_slot(
    baked_mesh: dict[str, Any],
    test_profile: dict[str, Any],
) -> None:
    """After test_00_bake, both devices must report the exact region, modem
    preset, slot, and channel name that the profile specified."""
    for role, state in baked_mesh.items():
        port = state["port"]
        live = info.device_info(port=port, timeout_s=8.0)
        lora = admin.get_config(section="lora", port=port)["config"]["lora"]

        expected_region = test_profile["USERPREFS_CONFIG_LORA_REGION"].rsplit("_", 1)[
            -1
        ]
        expected_preset = test_profile["USERPREFS_LORACONFIG_MODEM_PRESET"].rsplit(
            "_", 2
        )[-2:]
        expected_preset_str = "_".join(expected_preset)

        assert (
            live["region"] == expected_region
        ), f"{role}: region={live['region']!r}, expected {expected_region!r}"
        assert lora.get("modem_preset") in (
            expected_preset_str,
            expected_preset_str.upper(),
        ), f"{role}: modem_preset={lora.get('modem_preset')!r}, expected {expected_preset_str!r}"
        assert (
            int(lora.get("channel_num", 0))
            == test_profile["USERPREFS_LORACONFIG_CHANNEL_NUM"]
        ), f"{role}: channel_num={lora.get('channel_num')!r}"
        assert live["primary_channel"] == test_profile["USERPREFS_CHANNEL_0_NAME"]
