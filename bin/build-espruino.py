#!/usr/bin/env python3
"""
Build script for Espruino integration with Meshtastic.

This script performs two main tasks:
1. Builds Espruino embedded files using the Espruino Makefile
2. Converts TypeScript API definitions to JavaScript and wraps them in C++ headers

Generated files are placed in src/modules/EspruinoModule/
"""

import os
import sys
import subprocess
from pathlib import Path

# Get the project root directory (parent of bin/)
PROJECT_ROOT = Path(__file__).parent.parent.absolute()
ESPRUINO_DIR = PROJECT_ROOT / "Espruino"
MODULE_DIR = PROJECT_ROOT / "src" / "modules" / "EspruinoModule"
BUILD_DIR = MODULE_DIR / ".build"


def run_command(cmd, cwd=None, capture_output=False):
    """Run a command and check for errors."""
    try:
        if capture_output:
            result = subprocess.run(
                cmd,
                cwd=cwd,
                check=True,
                capture_output=True,
                text=True,
                shell=isinstance(cmd, str)
            )
            return result.stdout
        else:
            subprocess.run(cmd, cwd=cwd, check=True, shell=isinstance(cmd, str))
            return None
    except subprocess.CalledProcessError as e:
        print(f"Error running command: {' '.join(cmd) if isinstance(cmd, list) else cmd}")
        if capture_output and e.stderr:
            print(f"Error output: {e.stderr}")
        sys.exit(1)


def build_espruino_embedded():
    """Build Espruino embedded files using make."""
    print("=" * 60)
    print("Building Espruino embedded files...")
    print("=" * 60)
    
    if not ESPRUINO_DIR.exists():
        print(f"Error: Espruino directory not found at {ESPRUINO_DIR}")
        sys.exit(1)
    
    # Run make BOARD=EMBED
    print(f"Running make in {ESPRUINO_DIR}")
    run_command(["make", "BOARD=EMBED"], cwd=ESPRUINO_DIR)
    
    # Copy generated files to .build directory
    espruino_bin_dir = ESPRUINO_DIR / "bin"
    files_to_copy = ["espruino_embedded.c", "espruino_embedded.h", "jstypes.h"]
    
    # Ensure .build directory exists
    BUILD_DIR.mkdir(parents=True, exist_ok=True)
    
    print("\nCopying generated files to .build/...")
    for filename in files_to_copy:
        src = espruino_bin_dir / filename
        dst = BUILD_DIR / filename
        
        if not src.exists():
            print(f"Error: Generated file not found: {src}")
            sys.exit(1)
        
        # Read and write to copy (works cross-platform)
        dst.write_bytes(src.read_bytes())
        print(f"  Copied: {filename}")
    
    print("Espruino embedded files built successfully!")


def generate_cpp_header(js_code, is_minified):
    """Generate a C++ header file with embedded JavaScript code."""
    variant = "min" if is_minified else "debug"
    guard = f"JS_API_{variant.upper()}_H"
    
    header = f"""/*
 * Auto-generated file - DO NOT EDIT
 * Generated from api.ts using build-espruino.py
 * 
 * This file contains the JavaScript bootstrap code that initializes
 * the Meshtastic API for Espruino scripts.
 */

#ifndef {guard}
#define {guard}

// Bootstrap JavaScript code executed when Espruino initializes
const char* JS_API_BOOTSTRAP = R"js({js_code})js";

#endif // {guard}
"""
    return header


def build_api_headers():
    """Build JavaScript API headers from TypeScript using esbuild."""
    print("\n" + "=" * 60)
    print("Building API bootstrap headers...")
    print("=" * 60)
    
    api_ts = MODULE_DIR / "api.ts"
    if not api_ts.exists():
        print(f"Error: api.ts not found at {api_ts}")
        sys.exit(1)
    
    # Ensure .build directory exists
    BUILD_DIR.mkdir(parents=True, exist_ok=True)
    
    # Build debug version
    print("\nBuilding debug version...")
    debug_js = run_command(
        ["npx", "esbuild", "api.ts", "--bundle", "--format=iife", "--target=es2015", "--loader:.ts=ts"],
        cwd=MODULE_DIR,
        capture_output=True
    )
    
    # Write intermediate JavaScript file to .build
    debug_js_path = BUILD_DIR / "api.debug.js"
    debug_js_path.write_text(debug_js)
    print(f"  Wrote intermediate: .build/api.debug.js")
    
    debug_header = generate_cpp_header(debug_js, is_minified=False)
    debug_path = BUILD_DIR / "js_api.debug.h"
    debug_path.write_text(debug_header)
    print(f"  Generated: .build/js_api.debug.h")
    
    # Build minified version
    print("\nBuilding minified version...")
    min_js = run_command(
        ["npx", "esbuild", "api.ts", "--bundle", "--format=iife", "--target=es2015", "--minify", "--loader:.ts=ts"],
        cwd=MODULE_DIR,
        capture_output=True
    )
    
    # Write intermediate JavaScript file to .build
    min_js_path = BUILD_DIR / "api.min.js"
    min_js_path.write_text(min_js)
    print(f"  Wrote intermediate: .build/api.min.js")
    
    min_header = generate_cpp_header(min_js, is_minified=True)
    min_path = BUILD_DIR / "js_api.min.h"
    min_path.write_text(min_header)
    print(f"  Generated: .build/js_api.min.h")
    
    print("\nAPI bootstrap headers built successfully!")


def main():
    """Main build process."""
    print("\n" + "=" * 60)
    print("Espruino Build Script for Meshtastic")
    print("=" * 60)
    
    # Ensure module directory exists
    MODULE_DIR.mkdir(parents=True, exist_ok=True)
    
    # Task 1: Build Espruino embedded files
    build_espruino_embedded()
    
    # Task 2: Build API bootstrap headers
    build_api_headers()
    
    print("\n" + "=" * 60)
    print("Build completed successfully!")
    print("=" * 60)
    print("\nGenerated files in .build/:")
    print("  - espruino_embedded.c")
    print("  - espruino_embedded.h")
    print("  - jstypes.h")
    print("  - api.debug.js")
    print("  - api.min.js")
    print("  - js_api.debug.h")
    print("  - js_api.min.h")
    print()


if __name__ == "__main__":
    main()

