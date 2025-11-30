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


def find_site_packages(venv_dir):
    """Find site-packages directory in a venv (handles different Python versions)."""
    if not os.path.isdir(venv_dir):
        print(f"MPM: {venv_dir} does not exist or is not a directory")
        return None
    lib_dir = os.path.join(venv_dir, "lib")
    if not os.path.isdir(lib_dir):
        print(f"MPM: {lib_dir} does not exist or is not a directory")
        return None
    pattern = os.path.join(lib_dir, "python*/site-packages")
    matches = glob.glob(pattern)
    return matches[0] if matches else None


def add_to_path(directory, env_var="PATH", label=""):
    """Add directory to environment variable if it exists and isn't already present."""
    if not directory or not os.path.isdir(directory):
        print(f"MPM: {directory} does not exist or is not a directory")
        return False
    current = os.environ.get(env_var, "")
    if directory not in current:
        os.environ[env_var] = f"{directory}:{current}" if current else directory
        print(f"MPM: Added {label or directory} to {env_var}")
        return True
    return False


def add_to_sys_path(directory, label=""):
    """Add directory to sys.path if it exists and isn't already present."""
    if not directory or not os.path.isdir(directory):
        print(f"MPM: {directory} does not exist or is not a directory")
        return False
    if directory not in sys.path:
        sys.path.insert(0, directory)
        print(f"MPM: Added {directory} to sys.path")
        return True
    return False


# Find Poetry's .venv directory using PROJECT_DIR from PlatformIO env
# (__file__ is not available when exec'd by SCons)
project_dir = env["PROJECT_DIR"]  # type: ignore[name-defined]  # noqa: F821
print(f"MPM DEBUG: project_dir = {project_dir}")

# First, try to find mpm source and mpm's .venv for local development
# (mpm is at ../../mpm relative to firmware directory)
mpm_dir = os.path.join(project_dir, "..",  "mpm")
mpm_dir = os.path.abspath(mpm_dir)  # Resolve relative path
mpm_source_dir = os.path.join(mpm_dir, "src")
mpm_venv_dir = os.path.join(mpm_dir, ".venv")
firmware_venv_dir = os.path.join(project_dir, ".venv")
mpm_site_packages = find_site_packages(mpm_venv_dir)
firmware_site_packages = find_site_packages(firmware_venv_dir)
mpm_venv_bin = os.path.join(mpm_venv_dir, "bin") 
firmware_venv_bin = os.path.join(firmware_venv_dir, "bin")

print(f"MPM Directories:")
print(f"  mpm_dir: {mpm_dir}")
print(f"  mpm_source_dir: {mpm_source_dir}")
print(f"  mpm_venv_dir: {mpm_venv_dir}")
print(f"  firmware_venv_dir: {firmware_venv_dir}")
print(f"  mpm_site_packages: {mpm_site_packages}")
print(f"  firmware_site_packages: {firmware_site_packages}")
print(f"  mpm_venv_bin: {mpm_venv_bin}")
print(f"  firmware_venv_bin: {firmware_venv_bin}")

# These are in reverse order because they are prepended to sys.path
add_to_sys_path(firmware_site_packages, "firmware .venv") # Look for this last
add_to_sys_path(mpm_site_packages, "mpm .venv") # Look for this next
add_to_sys_path(mpm_source_dir, "mpm source") # Look for this first

# Add .venv/bin to PATH (prioritize mpm's .venv)
add_to_path(firmware_venv_bin, "PATH")
add_to_path(mpm_venv_bin, "PATH")

for site_packages in [mpm_site_packages, firmware_site_packages]:
    add_to_path(site_packages, "PYTHONPATH")

# Use the installed `mpm` package
from mesh_plugin_manager.build import init_plugins  # type: ignore[import]
from mesh_plugin_manager.build_utils import scan_plugins  # type: ignore[import]
from mesh_plugin_manager.proto import generate_all_protobuf_files  # type: ignore[import]

# Auto-initialize when imported by PlatformIO (not when run as __main__)
if __name__ != "__main__":
    try:
        init_plugins(env)  # type: ignore[name-defined]  # noqa: F821
    except NameError:
        # If env is missing for some reason, just skip auto-init
        pass

