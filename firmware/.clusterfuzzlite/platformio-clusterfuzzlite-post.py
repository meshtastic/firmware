"""PlatformIO build script (post: runs after other Meshtastic scripts)."""

import os
import shlex

from SCons.Script import DefaultEnvironment

env = DefaultEnvironment()

# Remove any static libraries from the LIBS environment. Static libraries are
# handled in platformio-clusterfuzzlite-pre.py.
static_libs = set(lib[2:] for lib in shlex.split(os.getenv("STATIC_LIBS")))
env.Replace(
    LIBS=[
        lib for lib in env["LIBS"] if not (isinstance(lib, str) and lib in static_libs)
    ],
)

# FrameworkArduino/portduino/main.cpp contains the "main" function the binary.
# The fuzzing framework also provides a "main" function and needs to be run
# before Meshtastic is started. We rename the "main" function for Meshtastic to
# "portduino_main" here so that it can be called inside the fuzzer.
env.AddPostAction(
    "$BUILD_DIR/FrameworkArduino/portduino/main.cpp.o",
    env.VerboseAction(
        " ".join(
            [
                "$OBJCOPY",
                "--redefine-sym=main=portduino_main",
                "$BUILD_DIR/FrameworkArduino/portduino/main.cpp.o",
            ]
        ),
        "Renaming main symbol to portduino_main",
    ),
)
