#!/usr/bin/env python3
"""
Mesh Plugin Manager (MPM) - PlatformIO build integration shim.

This script is intended to be used only as a PlatformIO extra_script.
It imports the real `mpm` package from Poetry's `.venv` and
exposes the build helpers expected by the firmware build.
"""

import os
import sys
import glob

# Provided by PlatformIO/SCons when used as `pre:` extra_script
Import("env")  # type: ignore[name-defined]  # noqa: F821

# Find Poetry's .venv directory using PROJECT_DIR from PlatformIO env
# (__file__ is not available when exec'd by SCons)
project_dir = env["PROJECT_DIR"]  # type: ignore[name-defined]  # noqa: F821
_venv_dir = os.path.join(project_dir, ".venv")

# Add .venv site-packages to sys.path
_site_packages_dirs = []
if os.path.isdir(_venv_dir):
    # Find site-packages directory (handles different Python versions)
    _lib_dir = os.path.join(_venv_dir, "lib")
    if os.path.isdir(_lib_dir):
        # Look for pythonX.Y/site-packages
        _site_packages_pattern = os.path.join(_lib_dir, "python*/site-packages")
        _site_packages_dirs = glob.glob(_site_packages_pattern)
        if _site_packages_dirs:
            _site_packages = _site_packages_dirs[0]
            if _site_packages not in sys.path:
                sys.path.insert(0, _site_packages)
                print(f"MPM: Added {_site_packages} to sys.path")

    # Add .venv/bin to PATH so console scripts can be found
    _venv_bin = os.path.join(_venv_dir, "bin")
    if os.path.isdir(_venv_bin):
        current_path = os.environ.get("PATH", "")
        if _venv_bin not in current_path:
            os.environ["PATH"] = f"{_venv_bin}:{current_path}" if current_path else _venv_bin
            print(f"MPM: Added {_venv_bin} to PATH")

    # Add .venv site-packages to PYTHONPATH so subprocesses (like nanopb_generator) can find modules
    if _site_packages_dirs:
        _site_packages = _site_packages_dirs[0]
        current_pythonpath = os.environ.get("PYTHONPATH", "")
        if _site_packages not in current_pythonpath:
            os.environ["PYTHONPATH"] = f"{_site_packages}:{current_pythonpath}" if current_pythonpath else _site_packages
            print(f"MPM: Added {_site_packages} to PYTHONPATH")

    # Add nanopb/generator to PATH so nanopb generator executables can be found
    if _site_packages_dirs:
        _nanopb_generator = os.path.join(_site_packages_dirs[0], "nanopb", "generator")
        if os.path.isdir(_nanopb_generator):
            current_path = os.environ.get("PATH", "")
            if _nanopb_generator not in current_path:
                os.environ["PATH"] = f"{_nanopb_generator}:{current_path}" if current_path else _nanopb_generator
                print(f"MPM: Added {_nanopb_generator} to PATH")

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

