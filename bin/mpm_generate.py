#!/usr/bin/env python3
"""
PlatformIO pre-build script to generate protobuf files and add plugin include paths.
"""

import os
import subprocess
import sys

Import("env")  # type: ignore[name-defined]  # noqa: F821
project_dir = env["PROJECT_DIR"]  # type: ignore[name-defined]  # noqa: F821

# Configure build_src_filter for plugins
# Exclude plugin root directories, include only src subdirectories
env.Append(SRC_FILTER=["-<../plugins/*>"])  # type: ignore[name-defined]  # noqa: F821
env.Append(SRC_FILTER=["+<../plugins/*/src>"])  # type: ignore[name-defined]  # noqa: F821
env.Append(BUILD_FLAGS=["-Isrc/modules"])  # type: ignore[name-defined]  # noqa: F821
# Scan for plugins and add include paths
plugins_dir = os.path.join(project_dir, "plugins")
if os.path.exists(plugins_dir) and os.path.isdir(plugins_dir):
    # Add plugins directory to include path so includes must be explicit like "lobbs/src/plugin.h"
    include_flag = f"-I{plugins_dir}"
    env.Append(BUILD_FLAGS=[include_flag])  # type: ignore[name-defined]  # noqa: F821
    rel_path = os.path.relpath(plugins_dir, project_dir)
    print(f"MPM: Added include path {rel_path}")


# Check if mpm command is available
mpm_available = False
try:
    result = subprocess.run(
        ["mpm", "version"],
        capture_output=True,
        text=True,
    )
    if result.returncode == 0:
        mpm_available = True
except (FileNotFoundError, subprocess.SubprocessError):
    pass

if not mpm_available:
    print("Warning: mpm command not found. Run 'pip install mesh-plugin-manager' to enable plugin support.", file=sys.stderr)

# Run mpm generate if available
if mpm_available:
    try:
        result = subprocess.run(
            ["mpm", "generate"],
            cwd=project_dir,
            capture_output=True,
            text=True,
        )
        if result.returncode != 0:
            print(f"Warning: mpm generate failed: {result.stderr}", file=sys.stderr)
        elif result.stdout:
            print(result.stdout)
    except Exception as e:
        print(f"Warning: Failed to run mpm generate: {e}", file=sys.stderr)
else:
    print("Skipping protobuf generation (mpm not available).", file=sys.stderr)
