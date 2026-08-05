"""Session-bake module — runs first in the tier order to flash both hub roles
with the session `test_profile`.

Ordered first by `pytest_collection_modifyitems` in `conftest.py` (bucket
-1) because `baked_mesh` only *verifies* state — it does not reflash. Without
the explicit order pin, the top-level path `tests/test_00_bake.py` falls
into the fallback bucket and sorts AFTER every tier, silently turning
`--force-bake` into a no-op for the tier tests.

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
import time
from typing import Any

import pytest
import serial  # type: ignore[import-untyped]
from meshtastic_mcp import admin, boards, flash, hw_tools, info

# Default envs for a common lab setup. Override per-role via env var.
_DEFAULT_ENVS = {
    "nrf52": "rak4631",
    "esp32s3": "heltec-v3",
}

_ESP32_ARCHES = {
    "esp32",
    "esp32-s2",
    "esp32s2",
    "esp32-s3",
    "esp32s3",
    "esp32-c3",
    "esp32c3",
    "esp32-c6",
    "esp32c6",
}
_NRF52_ARCHES = {"nrf52", "nrf52840", "nrf52832"}


def _wait_port_free(port: str, *, timeout_s: float = 15.0, role: str = "") -> None:
    """Block until `port` can be exclusively opened, or raise after `timeout_s`.

    Root cause for the retry loop: esptool / nrfutil / pio all take an
    *exclusive* serial port lock (fcntl LOCK_EX on macOS, EAGAIN otherwise).
    Anything that held the port recently — the TUI's startup `DevicePollerWorker._poll_once()`,
    a prior `device_info` call, a lingering `meshtastic-mcp` subprocess
    spawned by the operator's MCP host, or a stale `pio device monitor` —
    can still be holding it when `test_00_bake` reaches the flash step. The
    result is esptool exiting 2 in ~0.1s with `[Errno 35] Resource
    temporarily unavailable`.

    `pyserial.Serial(exclusive=True)` probes the same lock esptool takes;
    a brief open/close cycle is the cleanest way to verify the port is
    genuinely free before handing it to a subprocess we can't easily
    retry. 200 ms poll interval keeps the failure fast while giving the
    kernel time to release a just-closed descriptor.

    Raises AssertionError (rather than a generic TimeoutError) so the
    pytest summary shows the role + port + a hint at `lsof`.
    """
    role_prefix = f"{role}: " if role else ""
    deadline = time.monotonic() + timeout_s
    last_exc: BaseException | None = None
    while time.monotonic() < deadline:
        try:
            s = serial.Serial(port=port, exclusive=True, timeout=0.5)
        except Exception as exc:
            last_exc = exc
            time.sleep(0.2)
            continue
        try:
            s.close()
        except Exception:
            pass
        return
    raise AssertionError(
        f"{role_prefix}port {port} still busy after {timeout_s:.0f}s — "
        f"something else holds an exclusive lock. Last error: {last_exc!r}. "
        f"Identify the holder with `lsof {port}` and kill it; common "
        f"culprits are a lingering `meshtastic-mcp` subprocess from the "
        f"MCP host (.mcp.json) or a stale `pio device monitor`."
    )


def _prepare_nrf52_for_upload(port: str) -> str:
    """Kick the RAK4631 (or similar nRF52 USB-DFU board) into bootloader mode
    via 1200bps touch, then return the port where pio should upload.

    Adafruit bootloader on RAK4631 interprets 1200bps-open-close as 'enter
    DFU'. The device re-enumerates with a distinct USB VID/PID
    (0x239A/0x0029) at a different `/dev/cu.usbmodem*` path.

    `touch_1200bps` does the heavy lifting: bounded open/close, polls for the
    Adafruit-bootloader PID specifically, retries the touch up to twice.
    Fails loudly if the device doesn't enter DFU — no point trying pio
    upload against an app-mode device, it'll just hang.
    """
    result = flash.touch_1200bps(port=port, settle_ms=500, retries=2)
    if not result.get("ok"):
        raise AssertionError(
            f"nRF52 at {port} did not enter DFU bootloader after "
            f"{result.get('attempts')} 1200bps touches. Manual recovery: "
            f"double-tap the reset button on the board, then re-run. "
            f"Detected port set before/after touch was unchanged."
        )
    new_port = result["new_port"]
    # Small settle so pio/nrfutil sees a fully-ready CDC endpoint.
    time.sleep(1.0)
    return new_port


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

    # All architectures go through `pio run -t upload` — pio knows the right
    # protocol per variant (esptool for ESP32, adafruit-nrfutil for nRF52,
    # picotool for RP2040). We don't use `bin/device-install.sh` for ESP32
    # because it requires the external `mt-esp32s3-ota.bin` helper that's
    # downloaded from releases, not generated by the build.
    #
    # IMPORTANT: `pio run -t upload` on ESP32 only overwrites the APP
    # partition — the LittleFS partition (config + NodeDB) survives. That
    # means USERPREFS-baked defaults never take effect on a device that was
    # already provisioned, because NodeDB init prefers the saved config. To
    # force USERPREFS to apply cleanly, we erase the full chip first on
    # ESP32 boards. nRF52 DFU naturally wipes the user partition, so no
    # erase needed there.
    rec = boards.get_board(env)
    arch = rec.get("architecture") or ""
    # Make sure nothing else (TUI startup poll, MCP-host zombie, pio monitor)
    # is holding the port before we hand it to a subprocess. Self-heals the
    # [Errno 35] port-busy flake that otherwise fails the bake in ~0.1s.
    _wait_port_free(port, role=role)
    if arch in _NRF52_ARCHES:
        upload_port = _prepare_nrf52_for_upload(port)
    elif arch in _ESP32_ARCHES:
        # Full chip erase — wipes NVS + LittleFS so USERPREFS defaults apply.
        erase_result = hw_tools.esptool_erase_flash(port=port, confirm=True)
        assert erase_result["exit_code"] == 0, (
            f"{role}: esptool erase_flash failed:\n"
            f"{erase_result.get('stderr_tail', '')}"
        )
        upload_port = port
    else:
        upload_port = port

    # Post-erase, pre-upload: full chip erase on ESP32 drops the CDC
    # endpoint for a moment while the bootloader re-enters download mode.
    # Wait for the port to settle before pio reopens it for upload —
    # otherwise a fast machine can race and hit the same errno 35.
    if arch in _ESP32_ARCHES:
        _wait_port_free(upload_port, role=role, timeout_s=10.0)

    # NOTE: no `userprefs_overrides=` here. The session-scoped
    # `_session_userprefs` autouse fixture in conftest.py has already baked
    # the test profile into userPrefs.jsonc for the duration of the session
    # and will restore the original file at session end. A local
    # `temporary_overrides` here would be a no-op (file is already baked)
    # AND would cause the session fixture's teardown to see different
    # stat / mtime than it snapshotted — keep the mutation in one place.
    result = flash.flash(
        env=env,
        port=upload_port,
        confirm=True,
    )
    assert result["exit_code"] == 0, (
        f"{role} bake failed: exit={result['exit_code']}\n"
        f"stdout tail:\n{result.get('stdout_tail', '')}\n"
        f"stderr tail:\n{result.get('stderr_tail', '')}"
    )

    # Post-flash: for nRF52, the DFU process only overwrites the app
    # partition — the NVS region holding the existing NodeDB/config is
    # untouched, so the firmware will prefer the saved config over the
    # baked USERPREFS defaults. Trigger a full factory reset to wipe NVS
    # so USERPREFS takes effect on the next boot.
    #
    # ESP32 devices had their full flash erased BEFORE upload via
    # esptool_erase_flash, so they don't need this post-flash reset.
    if arch in _NRF52_ARCHES:
        # Give the device time to come up from DFU.
        time.sleep(8.0)
        # Wait for meshtastic to be responsive; `device_info` may take a
        # few seconds on the first post-flash boot.
        for _ in range(20):
            try:
                info.device_info(port=port, timeout_s=6.0)
                break
            except Exception:
                time.sleep(1.5)
        else:
            raise AssertionError(f"{role}: device didn't respond after DFU flash")
        # Trigger full factory reset (wipes NVS + identity)
        admin.factory_reset(port=port, confirm=True, full=True)
        # Wait for the device to reboot and come back with fresh config
        # populated from USERPREFS defaults.
        time.sleep(10.0)
        for _ in range(30):
            try:
                live = info.device_info(port=port, timeout_s=6.0)
                if live.get("my_node_num"):
                    break
            except Exception:
                pass
            time.sleep(2.0)
        else:
            raise AssertionError(f"{role}: device didn't return after factory_reset")


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
