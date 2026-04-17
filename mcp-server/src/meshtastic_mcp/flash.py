"""Build, clean, flash, and bootloader-entry operations.

Design: pio is the preferred path for every architecture via `flash()`. For
ESP32 factory flashes we shell out to `bin/device-install.sh` (which knows
about partition offsets and the OTA/littlefs partitions); for ESP32 OTA
updates we use `bin/device-update.sh`. Both scripts require the build
artifacts to exist, so these tools build first if needed.
"""

from __future__ import annotations

import subprocess
import time
from pathlib import Path
from typing import Any

import serial

from . import boards, config, devices, pio, userprefs

ESP32_ARCHES = {"esp32", "esp32s2", "esp32s3", "esp32c3", "esp32c6"}


class FlashError(RuntimeError):
    pass


def _require_confirm(confirm: bool, operation: str) -> None:
    if not confirm:
        raise FlashError(
            f"{operation} is destructive and requires confirm=True. "
            "This will overwrite firmware on the device."
        )


def _artifacts_for(env: str) -> list[Path]:
    build_dir = config.firmware_root() / ".pio" / "build" / env
    if not build_dir.is_dir():
        return []
    patterns = (
        "firmware*.bin",
        "firmware*.uf2",
        "firmware*.hex",
        "firmware*.zip",
        "firmware*.elf",
        "*.mt.json",
        "littlefs-*.bin",
    )
    out: list[Path] = []
    for pat in patterns:
        out.extend(sorted(build_dir.glob(pat)))
    return out


def _factory_bin_for(env: str) -> Path | None:
    build_dir = config.firmware_root() / ".pio" / "build" / env
    if not build_dir.is_dir():
        return None
    matches = sorted(build_dir.glob("firmware-*.factory.bin"))
    return matches[0] if matches else None


def _firmware_bin_for(env: str) -> Path | None:
    """Return the OTA-update firmware binary (app partition only)."""
    build_dir = config.firmware_root() / ".pio" / "build" / env
    if not build_dir.is_dir():
        return None
    # device-update.sh expects firmware-<env>-<version>.bin (not .factory.bin)
    matches = sorted(
        p
        for p in build_dir.glob("firmware-*.bin")
        if not p.name.endswith(".factory.bin")
    )
    return matches[0] if matches else None


def _userprefs_summary(active: dict[str, str]) -> dict[str, Any]:
    """Compact summary of which USERPREFS_* are baked into the build."""
    return {"count": len(active), "keys": sorted(active.keys())}


def build(
    env: str,
    with_manifest: bool = True,
    userprefs_overrides: dict[str, Any] | None = None,
) -> dict[str, Any]:
    """Run `pio run -e <env>` and return artifact paths.

    `userprefs_overrides` (optional): dict of `USERPREFS_<KEY>: value` to inject
    into userPrefs.jsonc for this build only. File is restored byte-for-byte
    on exit. Use `userprefs_set()` for persistent changes.
    """
    args = ["run", "-e", env]
    if with_manifest:
        args.extend(["-t", "mtjson"])
    with userprefs.temporary_overrides(userprefs_overrides) as effective:
        result = pio.run(args, timeout=pio.TIMEOUT_BUILD, check=False)
    return {
        "exit_code": result.returncode,
        "artifacts": [str(p) for p in _artifacts_for(env)],
        "stdout_tail": pio.tail_lines(result.stdout, 200),
        "stderr_tail": pio.tail_lines(result.stderr, 200),
        "duration_s": round(result.duration_s, 2),
        "userprefs": _userprefs_summary(effective),
    }


def clean(env: str) -> dict[str, Any]:
    """Run `pio run -e <env> -t clean`."""
    result = pio.run(["run", "-e", env, "-t", "clean"], timeout=120, check=False)
    return {
        "exit_code": result.returncode,
        "stdout_tail": pio.tail_lines(result.stdout, 200),
        "stderr_tail": pio.tail_lines(result.stderr, 200),
        "duration_s": round(result.duration_s, 2),
    }


def flash(
    env: str,
    port: str,
    confirm: bool = False,
    userprefs_overrides: dict[str, Any] | None = None,
) -> dict[str, Any]:
    """`pio run -e <env> -t upload --upload-port <port>`. All architectures.

    `userprefs_overrides` (optional): see `build()` — the rebuild-before-upload
    that pio performs will pick up the injected values.
    """
    _require_confirm(confirm, "flash")
    with userprefs.temporary_overrides(userprefs_overrides) as effective:
        result = pio.run(
            ["run", "-e", env, "-t", "upload", "--upload-port", port],
            timeout=pio.TIMEOUT_UPLOAD,
            check=False,
        )
    return {
        "exit_code": result.returncode,
        "stdout_tail": pio.tail_lines(result.stdout, 200),
        "stderr_tail": pio.tail_lines(result.stderr, 200),
        "duration_s": round(result.duration_s, 2),
        "userprefs": _userprefs_summary(effective),
    }


def _check_esp32_env(env: str) -> str:
    rec = boards.get_board(env)
    arch = rec.get("architecture")
    if arch not in ESP32_ARCHES:
        raise FlashError(
            f"Env {env!r} has architecture {arch!r}, not ESP32. "
            "Use `flash` for non-ESP32 boards."
        )
    return arch


def _run_install_script(script: Path, port: str, binary: Path) -> dict[str, Any]:
    """Invoke bin/device-install.sh or bin/device-update.sh."""
    t0 = time.monotonic()
    proc = subprocess.run(
        [str(script), "-p", port, "-f", str(binary)],
        cwd=str(config.firmware_root()),
        capture_output=True,
        text=True,
        timeout=pio.TIMEOUT_UPLOAD,
    )
    duration = time.monotonic() - t0
    return {
        "exit_code": proc.returncode,
        "stdout_tail": pio.tail_lines(proc.stdout, 200),
        "stderr_tail": pio.tail_lines(proc.stderr, 200),
        "duration_s": round(duration, 2),
    }


def erase_and_flash(
    env: str,
    port: str,
    confirm: bool = False,
    skip_build: bool = False,
    userprefs_overrides: dict[str, Any] | None = None,
) -> dict[str, Any]:
    """ESP32-only: full erase + factory flash via bin/device-install.sh.

    `userprefs_overrides`: baked into the factory.bin via a fresh build. If
    overrides are provided we always force a rebuild (skip_build=True errors
    in that case) since a cached factory.bin would not reflect the new prefs.
    """
    _require_confirm(confirm, "erase_and_flash")
    _check_esp32_env(env)

    if userprefs_overrides and skip_build:
        raise FlashError(
            "userprefs_overrides forces a rebuild so the factory.bin reflects "
            "the new values; skip_build=True is incompatible."
        )

    with userprefs.temporary_overrides(userprefs_overrides) as effective:
        # If overrides were provided, always build; otherwise only build if
        # no factory.bin is present.
        factory = _factory_bin_for(env)
        if factory is None or userprefs_overrides:
            if skip_build:
                raise FlashError(
                    f"No factory.bin found for env {env!r} and skip_build=True. "
                    "Run `build` first or set skip_build=False."
                )
            build_args = ["run", "-e", env, "-t", "mtjson"]
            build_result = pio.run(build_args, timeout=pio.TIMEOUT_BUILD, check=False)
            if build_result.returncode != 0:
                return {
                    "exit_code": build_result.returncode,
                    "stdout_tail": pio.tail_lines(build_result.stdout, 200),
                    "stderr_tail": pio.tail_lines(build_result.stderr, 200),
                    "duration_s": round(build_result.duration_s, 2),
                    "error": "build failed before erase_and_flash could run",
                    "userprefs": _userprefs_summary(effective),
                }
            factory = _factory_bin_for(env)
            if factory is None:
                raise FlashError(
                    f"Build succeeded but no factory.bin appeared in .pio/build/{env}/"
                )

        script = config.firmware_root() / "bin" / "device-install.sh"
        if not script.is_file():
            raise FlashError(f"device-install.sh not found at {script}")
        result = _run_install_script(script, port, factory)

    result["userprefs"] = _userprefs_summary(effective)
    return result


def update_flash(
    env: str,
    port: str,
    confirm: bool = False,
    skip_build: bool = False,
    userprefs_overrides: dict[str, Any] | None = None,
) -> dict[str, Any]:
    """ESP32-only: OTA app-partition update via bin/device-update.sh.

    `userprefs_overrides`: baked into the firmware.bin via a fresh build. If
    overrides are provided we always force a rebuild.
    """
    _require_confirm(confirm, "update_flash")
    _check_esp32_env(env)

    if userprefs_overrides and skip_build:
        raise FlashError(
            "userprefs_overrides forces a rebuild so the firmware.bin reflects "
            "the new values; skip_build=True is incompatible."
        )

    with userprefs.temporary_overrides(userprefs_overrides) as effective:
        firmware = _firmware_bin_for(env)
        if firmware is None or userprefs_overrides:
            if skip_build:
                raise FlashError(
                    f"No firmware.bin found for env {env!r} and skip_build=True. "
                    "Run `build` first or set skip_build=False."
                )
            build_args = ["run", "-e", env, "-t", "mtjson"]
            build_result = pio.run(build_args, timeout=pio.TIMEOUT_BUILD, check=False)
            if build_result.returncode != 0:
                return {
                    "exit_code": build_result.returncode,
                    "stdout_tail": pio.tail_lines(build_result.stdout, 200),
                    "stderr_tail": pio.tail_lines(build_result.stderr, 200),
                    "duration_s": round(build_result.duration_s, 2),
                    "error": "build failed before update_flash could run",
                    "userprefs": _userprefs_summary(effective),
                }
            firmware = _firmware_bin_for(env)
            if firmware is None:
                raise FlashError(
                    f"Build succeeded but no firmware.bin appeared in .pio/build/{env}/"
                )

        script = config.firmware_root() / "bin" / "device-update.sh"
        if not script.is_file():
            raise FlashError(f"device-update.sh not found at {script}")
        result = _run_install_script(script, port, firmware)

    result["userprefs"] = _userprefs_summary(effective)
    return result


def touch_1200bps(port: str, settle_ms: int = 250) -> dict[str, Any]:
    """Open port at 1200 baud, close immediately — triggers USB CDC bootloader.

    Works for: nRF52840 (Adafruit bootloader), ESP32-S3 (native USB download
    mode), RP2040 (when built with 1200bps-reset stdio), Arduino Leonardo/Micro.

    Afterward, polls `list_devices()` for up to 3 seconds to detect a new
    bootloader port that replaced the original application port.
    """
    before_ports = {d["port"] for d in devices.list_devices(include_unknown=True)}

    try:
        s = serial.Serial(port, 1200)
        # Some drivers need a brief settle before close; others disconnect
        # immediately when we set 1200 baud. Either is fine.
        time.sleep(settle_ms / 1000.0)
        try:
            s.close()
        except Exception:
            pass
    except serial.SerialException as exc:
        # Many boards drop the port mid-open when 1200 is set; that's expected.
        # Only treat "port doesn't exist" as a real error.
        if "No such file" in str(exc) or "could not open" in str(exc).lower():
            raise FlashError(f"Cannot open {port}: {exc}") from exc

    # Poll for a new port appearing (bootloader) or the old one disappearing
    deadline = time.monotonic() + 3.0
    new_port: str | None = None
    while time.monotonic() < deadline:
        time.sleep(0.2)
        current = {d["port"] for d in devices.list_devices(include_unknown=True)}
        added = current - before_ports
        if added:
            # Prefer a likely-meshtastic port among the newly appeared ones.
            current_list = devices.list_devices(include_unknown=True)
            added_records = [d for d in current_list if d["port"] in added]
            likely = next((d for d in added_records if d["likely_meshtastic"]), None)
            new_port = (likely or added_records[0])["port"]
            break
        if port not in current:
            # Old port went away entirely; bootloader may have shown up with a
            # different name. Give it a moment more.
            continue

    return {
        "ok": True,
        "former_port": port,
        "new_port": new_port,
    }
