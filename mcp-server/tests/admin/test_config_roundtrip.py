"""Admin: a config mutation survives a reboot.

This is the most-critical admin behavior not tested elsewhere. If
config persistence breaks in a firmware release, every deployed device
gets bricked on its next reboot (channels lost, region lost, owner lost,
everything back to Meshtastic stock). The fleet blast radius is "every
unit on every shelf" — easily worth one explicit test per release.

Pattern: single-device (``baked_single``, one test per role). Mutate a
benign, easy-to-observe LoRa field (``lora.hop_limit``), confirm
pre-reboot, reboot, rediscover port (nRF52 may re-enumerate), verify
the value survived, restore original for downstream tests.

Why ``lora.hop_limit`` specifically:
  * Non-destructive — doesn't change region, channel, or PSK, so
    downstream mesh tests still work regardless of the flipped value.
  * Bounded small-integer (1..7) — easy to flip to a definitively
    different value and read back.
  * Persisted via ``writeConfig("lora")`` which is the same path
    every other LoRa config mutation uses, so we're really testing
    the whole lora-config persistence pipeline end-to-end.
"""

from __future__ import annotations

import time
from typing import Any

import pytest
from meshtastic_mcp import admin, info

from .._port_discovery import resolve_port_by_role


def _get_hop_limit(port: str) -> int:
    """Read `lora.hop_limit` from the device's current config."""
    lora = admin.get_config("lora", port=port).get("config", {}).get("lora", {})
    hl = lora.get("hop_limit")
    assert isinstance(hl, int), (
        f"lora.hop_limit missing or non-int in get_config response: " f"{lora!r}"
    )
    return hl


@pytest.mark.timeout(180)
def test_lora_hop_limit_survives_reboot(
    baked_single: dict[str, Any],
    wait_until,
) -> None:
    """Runs once per connected role. Mutates `lora.hop_limit`, reboots,
    verifies the new value is still there after the device comes back.
    """
    role = baked_single["role"]
    port = baked_single["port"]

    original = _get_hop_limit(port)
    # Flip to a definitively different value within the protocol's
    # valid range (1..7 per LoRaConfig.hop_limit comment). Pick 5 if
    # current is != 5, else 4.
    new_value = 5 if original != 5 else 4

    try:
        admin.set_config("lora.hop_limit", new_value, port=port)

        # Pre-reboot sanity: the write reached the device and
        # get_config reflects it in-memory. If this fails, the persist
        # test below is moot — something's wrong with the write path
        # itself, not with persistence.
        assert _get_hop_limit(port) == new_value, (
            f"pre-reboot readback failed: set {new_value}, got "
            f"{_get_hop_limit(port)}"
        )

        # Reboot. `seconds=3` gives the Python client time to
        # disconnect cleanly; sleep long enough for the boot to start
        # before we begin polling.
        admin.reboot(port=port, confirm=True, seconds=3)
        time.sleep(8.0)

        # nRF52 re-enumerates on reboot → rediscover.
        port = resolve_port_by_role(role, timeout_s=60.0)
        wait_until(
            lambda: info.device_info(port=port, timeout_s=5.0).get("my_node_num")
            is not None,
            timeout=60,
            backoff_start=1.0,
        )

        # The assertion this test exists for: the mutation persisted
        # across the reboot cycle through NVS / LittleFS / UICR.
        post = _get_hop_limit(port)
        assert post == new_value, (
            f"lora.hop_limit did not survive reboot: set to {new_value} "
            f"pre-reboot, read back {post} post-reboot. Config persistence "
            f"is broken — downstream fleet impact would be total."
        )
    finally:
        # Restore so downstream tests see the original hop_limit.
        # Wrapped in its own try to avoid masking the real assertion
        # if the restore itself races the reboot — the worst case
        # there is a non-default hop_limit sticks around, which is
        # benign (mesh still works at hop_limit 3 or 5).
        try:
            admin.set_config("lora.hop_limit", original, port=port)
        except Exception:
            pass
