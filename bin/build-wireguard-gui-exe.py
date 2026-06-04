#!/usr/bin/env python3
"""Build a Windows executable for the WireGuard GUI."""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
VENV_DIR = REPO_ROOT / ".wireguard-gui-venv"
SETUP_SCRIPT = REPO_ROOT / "bin" / "setup-wireguard-gui.py"
GUI_SCRIPT = REPO_ROOT / "bin" / "wireguard-gui.py"
CONFIG_SCRIPT = REPO_ROOT / "bin" / "wireguard-config.py"
APP_NAME = "MeshtasticWireGuardConfigurator"


def _venv_python() -> Path:
    if sys.platform == "win32":
        return VENV_DIR / "Scripts" / "python.exe"
    return VENV_DIR / "bin" / "python"


def _run(command: list[str]) -> None:
    print("+ " + " ".join(command), flush=True)
    subprocess.run(command, cwd=REPO_ROOT, check=True)


def main() -> int:
    if sys.platform != "win32":
        print("This build script is intended for Windows executable packaging.")
        return 1

    python = _venv_python()
    if not python.exists():
        _run([sys.executable, str(SETUP_SCRIPT)])

    _run([str(python), "-m", "pip", "install", "pyinstaller"])
    separator = ";"
    _run(
        [
            str(python),
            "-m",
            "PyInstaller",
            "--noconfirm",
            "--clean",
            "--onefile",
            "--windowed",
            "--name",
            APP_NAME,
            "--add-data",
            f"{CONFIG_SCRIPT}{separator}.",
            "--collect-submodules",
            "meshtastic.protobuf",
            "--hidden-import",
            "meshtastic.serial_interface",
            "--hidden-import",
            "serial.tools.list_ports",
            str(GUI_SCRIPT),
        ]
    )

    exe = REPO_ROOT / "dist" / f"{APP_NAME}.exe"
    print()
    print(f"Built: {exe}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
