"""Full power-cycle round-trip: off → verify gone → on → verify identity
preserved.

Parametrized over every connected role. Validates both the uhubctl
plumbing AND that the device survives a hard reset with the same
`my_node_num` (no firmware-level identity regeneration).
"""

from __future__ import annotations

import time

import pytest
from meshtastic_mcp import info
from tests import _power
from tests._port_discovery import resolve_port_by_role


@pytest.mark.timeout(180)
def test_power_cycle_preserves_node_identity(
    baked_single: dict[str, object],
) -> None:
    role = baked_single["role"]
    pre_port = baked_single["port"]
    pre_node_num = baked_single["my_node_num"]
    pre_fw = baked_single.get("firmware_version")

    # Record pre-cycle state.
    pre_info = info.device_info(port=pre_port, timeout_s=5.0)
    assert pre_info.get("my_node_num") == pre_node_num

    # Power off; confirm the device actually disappears from list_devices.
    _power.power_off(role)
    try:
        _power.wait_for_absence(role, timeout_s=10.0)
    except TimeoutError:
        # If it didn't disappear, power it back on so we don't leave the
        # hub in a weird state for the next test.
        _power.power_on(role)
        resolve_port_by_role(role, timeout_s=30.0)
        pytest.fail(f"device {role!r} stayed visible after power_off")

    # Power back on + re-discover port.
    _power.power_on(role)
    time.sleep(0.5)  # head-start before polling
    new_port = resolve_port_by_role(role, timeout_s=30.0)

    # Give the firmware a moment to finish boot before we hit it with admin.
    time.sleep(2.0)

    post_info = info.device_info(port=new_port, timeout_s=10.0)
    assert post_info.get("my_node_num") == pre_node_num, (
        f"my_node_num changed across power-cycle: pre={pre_node_num:#x} "
        f"post={post_info.get('my_node_num'):#x}"
    )
    # Firmware version must match (same bake, not a re-flash).
    if pre_fw:
        assert post_info.get("firmware_version") == pre_fw, (
            f"firmware changed across cycle: pre={pre_fw} "
            f"post={post_info.get('firmware_version')}"
        )
