"""Resolves the firmware repo root and the binaries we invoke.

Everything that needs a path (the firmware root, `pio`, `esptool`, etc.) goes
through this module so the rest of the package never calls `shutil.which` or
parses environment variables directly.
"""

from __future__ import annotations

import os
import shutil
from pathlib import Path
from typing import Iterable


class ConfigError(RuntimeError):
    """Raised when a required path or binary cannot be resolved."""


def firmware_root() -> Path:
    """Resolve the root of the Meshtastic firmware repo.

    Resolution order:
      1. `MESHTASTIC_FIRMWARE_ROOT` env var.
      2. Walk up from `cwd` looking for a directory with `platformio.ini`.
    """
    env = os.environ.get("MESHTASTIC_FIRMWARE_ROOT")
    if env:
        root = Path(env).expanduser().resolve()
        if not (root / "platformio.ini").is_file():
            raise ConfigError(
                f"MESHTASTIC_FIRMWARE_ROOT={env!r} does not contain platformio.ini"
            )
        return root

    cur = Path.cwd().resolve()
    for candidate in (cur, *cur.parents):
        if (candidate / "platformio.ini").is_file():
            return candidate
    raise ConfigError(
        "Could not locate Meshtastic firmware root. Set MESHTASTIC_FIRMWARE_ROOT "
        "to the directory containing platformio.ini."
    )


def _first_existing(paths: Iterable[Path]) -> Path | None:
    for p in paths:
        if p and p.is_file() and os.access(p, os.X_OK):
            return p
    return None


def pio_bin() -> Path:
    """Resolve the `pio` binary.

    Order: MESHTASTIC_PIO_BIN → ~/.platformio/penv/bin/pio (PlatformIO keeps
    this one current) → `pio` on PATH → `platformio` on PATH.
    """
    env = os.environ.get("MESHTASTIC_PIO_BIN")
    if env:
        p = Path(env).expanduser()
        if p.is_file() and os.access(p, os.X_OK):
            return p
        raise ConfigError(f"MESHTASTIC_PIO_BIN={env!r} is not an executable file")

    penv = Path.home() / ".platformio" / "penv" / "bin" / "pio"
    if penv.is_file() and os.access(penv, os.X_OK):
        return penv

    for name in ("pio", "platformio"):
        w = shutil.which(name)
        if w:
            return Path(w)

    raise ConfigError(
        "Could not find `pio`. Install PlatformIO (https://platformio.org/install/cli) "
        "or set MESHTASTIC_PIO_BIN."
    )


def _hw_tool(env_var: str, names: tuple[str, ...], install_hint: str) -> Path:
    """Shared resolver for esptool / nrfutil / picotool.

    Prefers the firmware repo's own `.venv/bin/<name>` (esptool lives there),
    then PATH.
    """
    env = os.environ.get(env_var)
    if env:
        p = Path(env).expanduser()
        if p.is_file() and os.access(p, os.X_OK):
            return p
        raise ConfigError(f"{env_var}={env!r} is not an executable file")

    try:
        venv_bin = firmware_root() / ".venv" / "bin"
    except ConfigError:
        venv_bin = None

    for name in names:
        if venv_bin is not None:
            p = venv_bin / name
            if p.is_file() and os.access(p, os.X_OK):
                return p

    for name in names:
        w = shutil.which(name)
        if w:
            return Path(w)

    raise ConfigError(
        f"Could not find `{names[0]}`. {install_hint} "
        f"Or set {env_var} to an absolute path."
    )


def esptool_bin() -> Path:
    return _hw_tool(
        "MESHTASTIC_ESPTOOL_BIN",
        ("esptool", "esptool.py"),
        "Install via `pip install esptool`.",
    )


def nrfutil_bin() -> Path:
    return _hw_tool(
        "MESHTASTIC_NRFUTIL_BIN",
        ("nrfutil", "adafruit-nrfutil"),
        "Install via `pip install adafruit-nrfutil` or download Nordic nRF Util.",
    )


def picotool_bin() -> Path:
    return _hw_tool(
        "MESHTASTIC_PICOTOOL_BIN",
        ("picotool",),
        "Install via `brew install picotool` or build from https://github.com/raspberrypi/picotool.",
    )


def uhubctl_bin() -> Path:
    return _hw_tool(
        "MESHTASTIC_UHUBCTL_BIN",
        ("uhubctl",),
        "Install via `brew install uhubctl` (macOS) or `apt install uhubctl` "
        "(Debian/Ubuntu). On Linux without the udev rules, or on older macOS "
        "with certain hubs, you may need to run via `sudo`: "
        "https://github.com/mvp/uhubctl#linux-usb-permissions",
    )
