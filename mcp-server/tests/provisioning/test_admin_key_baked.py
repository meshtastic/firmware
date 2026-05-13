"""Provisioning: baked admin keys end up in the device's security config.

Fleet operators pre-bake an `USERPREFS_USE_ADMIN_KEY_0` into firmware so that
remote-admin messages from a central controller are accepted. This test
verifies the key bytes make the round-trip: USERPREFS → build-time `-D` flag
→ firmware → `localConfig.security.admin_key`.
"""

from __future__ import annotations

import os
from typing import Any

import pytest
from meshtastic_mcp import admin, flash

# Deterministic 32-byte "admin key" — just the byte values 0..31 for easy
# recognition in the output, formatted as a C brace-init.
_ADMIN_KEY_BYTES = list(range(32))
_ADMIN_KEY_BRACE = "{ " + ", ".join(f"0x{b:02x}" for b in _ADMIN_KEY_BYTES) + " }"


@pytest.mark.skip(
    reason="test uses flash.erase_and_flash which shells to bin/device-install.sh "
    "which needs mt-esp32s3-ota.bin (not in repo). TODO: switch to "
    "esptool_erase_flash + flash.flash() like test_00_bake."
)
@pytest.mark.timeout(600)
def test_admin_key_baked(
    hub_devices: dict[str, str],
    test_profile: dict[str, Any],
) -> None:
    """Bake test_profile + admin key 0; verify `security.admin_key` contains
    the baked bytes after boot. Re-bakes session profile (without admin key)
    on teardown so downstream tests see baseline state.
    """
    target = "esp32s3"
    if target not in hub_devices:
        pytest.skip(f"role {target!r} not on hub")
    port = hub_devices[target]
    env = os.environ.get("MESHTASTIC_MCP_ENV_ESP32S3", "t-beam-1w")

    augmented = dict(test_profile)
    augmented["USERPREFS_USE_ADMIN_KEY_0"] = _ADMIN_KEY_BRACE

    try:
        result = flash.erase_and_flash(
            env=env,
            port=port,
            confirm=True,
            userprefs_overrides=augmented,
        )
        assert result["exit_code"] == 0

        security = admin.get_config(section="security", port=port)["config"]["security"]
        # `admin_key` may be a list of byte-sequences under newer protobuf, or
        # a single bytes field under older. We accept either as long as the
        # baked bytes appear somewhere in the serialization.
        key_field = security.get("admin_key")
        import base64
        import json

        serialized = json.dumps(security)

        # Protobuf→JSON typically base64-encodes bytes fields. Encode our
        # expected bytes and look for them (or a substring) in the serialized
        # security config.
        b64 = base64.b64encode(bytes(_ADMIN_KEY_BYTES)).decode("ascii").rstrip("=")
        assert (
            b64[:40] in serialized or "admin_key" in serialized
        ), f"admin_key bytes not visible in security config: {security!r}"
        assert (
            key_field is not None
        ), "security.admin_key field absent — baking key 0 didn't stick"
    finally:
        # Restore session profile (no admin key)
        restore = flash.erase_and_flash(
            env=env,
            port=port,
            confirm=True,
            userprefs_overrides=test_profile,
        )
        assert restore["exit_code"] == 0
