"""Provisioning (negative): firmware baked WITHOUT
`USERPREFS_CONFIG_LORA_REGION` must refuse to transmit.

Real operator concern: FCC compliance. A device shipped without an explicit
region setting must not emit RF until the operator sets a region — this test
proves the firmware honors that invariant when the USERPREFS bake deliberately
omits the region key.

Teardown re-bakes the session `test_profile` so downstream shared-state
tiers (admin/mesh/telemetry) still see a correctly configured mesh.
"""

from __future__ import annotations

from typing import Any

import pytest
from meshtastic_mcp import admin, flash, info


@pytest.mark.skip(
    reason="test uses flash.erase_and_flash which shells to bin/device-install.sh "
    "which needs mt-esp32s3-ota.bin (not in repo). TODO: switch to "
    "esptool_erase_flash + flash.flash() like test_00_bake."
)
@pytest.mark.timeout(600)
def test_unset_region_blocks_tx(
    hub_devices: dict[str, str],
    no_region_profile: dict[str, Any],
    test_profile: dict[str, Any],
    serial_capture,
) -> None:
    """Bake a device with no LoRa region, then assert:
      1. `config.lora.region` reads as "UNSET" (or 0).
      2. An attempt to `send_text` surfaces a refusal — either the meshtastic
         SDK raises, or the serial log contains a clear "region unset" marker.

    Always re-bakes the session test_profile in the finalizer so downstream
    categories are not left with a broken device.
    """
    target = "esp32s3"
    if target not in hub_devices:
        pytest.skip(f"role {target!r} not on hub")
    port = hub_devices[target]

    # Pick the right env for this role — must match what test_00_bake used.
    import os

    env = os.environ.get("MESHTASTIC_MCP_ENV_ESP32S3", "t-beam-1w")

    # Capture serial before the bake to see the "region unset" log line on boot
    cap = serial_capture(target, env=env)

    # Bake without region
    result = flash.erase_and_flash(
        env=env,
        port=port,
        confirm=True,
        userprefs_overrides=no_region_profile,
    )
    assert (
        result["exit_code"] == 0
    ), f"bake of no_region_profile failed:\n{result.get('stderr_tail', '')}"

    try:
        # After bake, device should boot with region=UNSET
        live = info.device_info(port=port, timeout_s=12.0)
        assert live.get("region") in (None, "UNSET", "UNSET_0", ""), (
            f"expected region UNSET after baking without region pref; "
            f"got {live.get('region')!r}"
        )

        # Attempting to send a message should either raise or be logged as
        # refused. The meshtastic SDK's sendText may raise in this condition,
        # or it may accept the call but the firmware rejects on air.
        send_failed = False
        try:
            admin.send_text(text="should not transmit", port=port)
        except Exception:
            send_failed = True

        # Give the firmware a moment to log anything
        import time as _time

        _time.sleep(3.0)
        log = "\n".join(cap.snapshot(max_lines=2000))
        # We expect EITHER the send raised at the Python layer, OR the serial
        # log explicitly says region is unset.
        log_says_unset = any(
            marker in log.lower()
            for marker in ("region unset", "region is unset", "no region set")
        )
        assert send_failed or log_says_unset, (
            "expected send to fail or log 'region unset'; neither happened.\n"
            f"log tail:\n{log[-2000:]}"
        )
    finally:
        # Re-bake the session profile so downstream tests work.
        restore = flash.erase_and_flash(
            env=env,
            port=port,
            confirm=True,
            userprefs_overrides=test_profile,
        )
        assert restore["exit_code"] == 0, (
            "CRITICAL: failed to re-bake session profile after "
            "no-region test; downstream tests will fail."
        )
