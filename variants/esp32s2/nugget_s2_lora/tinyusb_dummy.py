#!/usr/bin/env python3
# trunk-ignore-all(ruff/F821)
# trunk-ignore-all(flake8/F821): For SConstruct imports
import shutil
from pathlib import Path

Import("env")

project_dir = Path(env.subst("$PROJECT_DIR"))
dummy_dir = project_dir / ".dummy"
platform_build_lib = Path(env.PioPlatform().get_dir()) / "builder" / "build_lib"
tinyusb_include = (
    Path(env.PioPlatform().get_package_dir("framework-arduinoespressif32-libs"))
    / "esp32s2"
    / "include"
    / "arduino_tinyusb"
)

if not dummy_dir.exists():
    shutil.copytree(platform_build_lib, dummy_dir)

# pioarduino links a temporary app before project sources; give that pass the Nugget stubs too.
cmake_content = f"""idf_component_register(
    SRCS
        "sketch.cpp"
        "arduino-lib-builder-gcc.c"
        "arduino-lib-builder-cpp.cpp"
        "arduino-lib-builder-as.S"
        "{project_dir / "src/platform/esp32/NuggetS2TinyUSBCompat.cpp"}"
    INCLUDE_DIRS
        "."
        "{tinyusb_include / "include"}"
        "{tinyusb_include / "tinyusb/src"}"
)
"""

cmake_path = dummy_dir / "CMakeLists.txt"
if not cmake_path.exists() or cmake_path.read_text() != cmake_content:
    cmake_path.write_text(cmake_content)
