"""PlatformIO post-action to strip DWARF symbols from firmware artifacts."""
from __future__ import annotations

from SCons.Script import Import  # type: ignore[attr-defined]

Import("env")

# Strip debug info from the ELF so derivative artifacts remain lean.
env.AddPostAction(
    "$BUILD_DIR/${PROGNAME}.elf",
    env.VerboseAction("$OBJCOPY --strip-debug $TARGET", "Stripping debug symbols from $TARGET"),
)
