"""Session-bake module — runs first (alphabetical collection) to flash both hub
roles with the session `test_profile`.

Skipped entirely when `--assume-baked` is passed. All downstream hardware
tests either depend on `baked_mesh` (which verifies state) or do their own
per-test bake (provisioning/fleet tiers), so failing here gives one clear
actionable failure instead of a cascade of mismatches.

Hardware-specific env names live in a small role→env map at the top of this
file; override by setting `MESHTASTIC_MCP_ENV_<ROLE>` env vars (e.g.
`MESHTASTIC_MCP_ENV_NRF52=heltec-mesh-node-t114`).
"""

from __future__ import annotations

import os
from typing import Any

import pytest
from meshtastic_mcp import flash, info

# Default envs for a common lab setup. Override per-role via env var.
_DEFAULT_ENVS = {
    "nrf52": "heltec-mesh-node-t114",
    "esp32s3": "t-beam-1w",
}


def _env_for(role: str) -> str:
    override = os.environ.get(f"MESHTASTIC_MCP_ENV_{role.upper()}")
    if override:
        return override
    if role not in _DEFAULT_ENVS:
        pytest.fail(
            f"no default PlatformIO env for role {role!r}. "
            f"Set MESHTASTIC_MCP_ENV_{role.upper()} to the env name."
        )
    return _DEFAULT_ENVS[role]


def _bake_role(
    role: str,
    port: str,
    test_profile: dict[str, Any],
    force_bake: bool,
) -> None:
    """Bake + boot + verify for a single role. Skips if already baked unless
    `--force-bake` was passed."""
    env = _env_for(role)

    # If not forcing, check if already baked with session profile.
    if not force_bake:
        try:
            live = info.device_info(port=port, timeout_s=8.0)
            # Quick heuristic: region matches and primary channel matches.
            expected_region_short = test_profile["USERPREFS_CONFIG_LORA_REGION"].rsplit(
                "_", 1
            )[-1]
            if (
                live.get("region") == expected_region_short
                and live.get("primary_channel")
                == test_profile["USERPREFS_CHANNEL_0_NAME"]
            ):
                pytest.skip(
                    f"{role} at {port} already baked with session profile "
                    f"(pass --force-bake to reflash)"
                )
        except Exception:
            # If we can't query, fall through and bake anyway.
            pass

    result = flash.erase_and_flash(
        env=env,
        port=port,
        confirm=True,
        skip_build=False,
        userprefs_overrides=test_profile,
    )
    assert result["exit_code"] == 0, (
        f"{role} bake failed: exit={result['exit_code']}\n"
        f"stdout tail:\n{result.get('stdout_tail', '')}\n"
        f"stderr tail:\n{result.get('stderr_tail', '')}"
    )


@pytest.mark.timeout(600)
def test_bake_nrf52(
    hub_devices: dict[str, str],
    test_profile: dict[str, Any],
    request: pytest.FixtureRequest,
) -> None:
    """Flash the nRF52840 role with the session test profile."""
    if "nrf52" not in hub_devices:
        pytest.skip("nRF52 not detected on hub")
    _bake_role(
        role="nrf52",
        port=hub_devices["nrf52"],
        test_profile=test_profile,
        force_bake=request.config.getoption("--force-bake"),
    )


@pytest.mark.timeout(600)
def test_bake_esp32s3(
    hub_devices: dict[str, str],
    test_profile: dict[str, Any],
    request: pytest.FixtureRequest,
) -> None:
    """Flash the ESP32-S3 role with the session test profile."""
    if "esp32s3" not in hub_devices:
        pytest.skip("ESP32-S3 not detected on hub")
    _bake_role(
        role="esp32s3",
        port=hub_devices["esp32s3"],
        test_profile=test_profile,
        force_bake=request.config.getoption("--force-bake"),
    )
