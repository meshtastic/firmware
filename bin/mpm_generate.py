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
    plugin_dirs = [
        d
        for d in os.listdir(plugins_dir)
        if os.path.isdir(os.path.join(plugins_dir, d)) and not d.startswith(".")
    ]
    
    for plugin_name in plugin_dirs:
        plugin_src_path = os.path.join(plugins_dir, plugin_name, "src")
        if os.path.isdir(plugin_src_path):
            # Add include path for this plugin
            include_flag = f"-I{plugin_src_path}"
            env.Append(BUILD_FLAGS=[include_flag])  # type: ignore[name-defined]  # noqa: F821
            rel_path = os.path.relpath(plugin_src_path, project_dir)
            print(f"MPM: Added include path {rel_path}")



print(env["SRC_FILTER"])
print(env["BUILD_FLAGS"])

# Run mpm generate
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
except FileNotFoundError:
    print("Warning: mpm command not found. Skipping protobuf generation.", file=sys.stderr)
except Exception as e:
    print(f"Warning: Failed to run mpm generate: {e}", file=sys.stderr)
