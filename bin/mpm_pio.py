#!/usr/bin/env python3
"""
Mesh Plugin Manager (MPM) - PlatformIO build integration shim.

This script is intended to be used only as a PlatformIO extra_script.
It imports the real `mpm` package (vendored via `pyvendor/`) and
exposes the build helpers expected by the firmware build.
"""

import os
import sys

try:
    # Provided by PlatformIO/SCons when used as `pre:` extra_script
    Import("env")  # type: ignore[name-defined]  # noqa: F821
    IS_PLATFORMIO = True
except Exception:
    IS_PLATFORMIO = False

# Add vendored Python dependencies (installed with `pip install -r requirements.txt -t pyvendor`)
if IS_PLATFORMIO:
    # Use PROJECT_DIR from PlatformIO env (__file__ is not available when exec'd by SCons)
    project_dir = env["PROJECT_DIR"]  # type: ignore[name-defined]  # noqa: F821
    _pyvendor = os.path.join(project_dir, "pyvendor")
else:
    # Standalone execution: use __file__ if available, otherwise cwd
    try:
        _here = os.path.dirname(__file__)
        _pyvendor = os.path.abspath(os.path.join(_here, "..", "pyvendor"))
    except NameError:
        _pyvendor = os.path.join(os.getcwd(), "pyvendor")

if os.path.isdir(_pyvendor) and _pyvendor not in sys.path:
    sys.path.insert(0, _pyvendor)

if IS_PLATFORMIO:
    # Use the installed `mpm` package
    from mpm.build import init_plugins  # type: ignore[import]
    from mpm.build_utils import scan_plugins  # type: ignore[import]
    from mpm.proto import generate_all_protobuf_files  # type: ignore[import]

    # Auto-initialize when imported by PlatformIO (not when run as __main__)
    if __name__ != "__main__":
        try:
            init_plugins(env)  # type: ignore[name-defined]  # noqa: F821
        except NameError:
            # If env is missing for some reason, just skip auto-init
            pass
else:
    # Standalone execution: delegate to the real CLI for convenience
    if __name__ == "__main__":
        from mpm.cli import main  # type: ignore[import]

        main()

