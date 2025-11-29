#!/usr/bin/env python3
"""
Mesh Plugin Manager (MPM) - PlatformIO build integration shim.

This script is intended to be used only as a PlatformIO extra_script.
It imports the real `mpm` package (installed via requirements.txt) and
exposes the build helpers expected by the firmware build.
"""

try:
    # Provided by PlatformIO/SCons when used as `pre:` extra_script
    Import("env")  # type: ignore[name-defined]  # noqa: F821
    IS_PLATFORMIO = True
except Exception:
    IS_PLATFORMIO = False

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

