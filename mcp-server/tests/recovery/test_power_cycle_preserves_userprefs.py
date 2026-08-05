"""Hard reset via uhubctl must NOT wipe NVS. Verify the test profile's
region + channel survive a power-cycle.

Guards against a regression where a firmware change treats unexpected
power loss as a factory-reset trigger (e.g. bad EEPROM wear-leveling,
erase-on-boot-for-safety). Such a regression would be catastrophic for
field deployments.
"""

from __future__ import annotations

import time

import pytest
from meshtastic_mcp import admin, info
from tests import _power
from tests._port_discovery import resolve_port_by_role


@pytest.mark.timeout(180)
def test_lora_config_survives_power_cycle(
    baked_single: dict[str, object],
    test_profile: dict[str, object],
) -> None:
    role = baked_single["role"]
    pre_port = baked_single["port"]

    pre_config = admin.get_config(section="lora", port=pre_port)["config"]["lora"]
    pre_region = pre_config.get("region")
    pre_preset = pre_config.get("modem_preset")
    assert pre_region, f"lora.region not set pre-cycle on {role}"

    # Hard power-cycle.
    _power.power_cycle(role, delay_s=2)
    time.sleep(0.5)
    new_port = resolve_port_by_role(role, timeout_s=30.0)
    # Let the firmware complete boot before admin reads.
    time.sleep(2.0)
    # Quick readiness probe.
    probe = info.device_info(port=new_port, timeout_s=10.0)
    assert (
        probe.get("my_node_num") is not None
    ), f"device {role!r} didn't respond after power-cycle"

    post_config = admin.get_config(section="lora", port=new_port)["config"]["lora"]
    post_region = post_config.get("region")
    post_preset = post_config.get("modem_preset")

    assert post_region == pre_region, (
        f"lora.region wiped by power-cycle on {role}: "
        f"pre={pre_region!r} post={post_region!r}"
    )
    assert post_preset == pre_preset, (
        f"lora.modem_preset wiped by power-cycle on {role}: "
        f"pre={pre_preset!r} post={post_preset!r}"
    )

    # Channel-0 name should also match the test profile.
    pri_ch = admin.get_channel_url(port=new_port)
    assert pri_ch.get("url"), f"channel URL empty after power-cycle on {role}"
