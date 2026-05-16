"""
PlatformIO doesn't natively link .o files vendored inside a library directory.
The Morse Micro SDK ships the chip firmware (mm6108.mbin.o) and per-region BCF
(bcf_mf08651_us.mbin.o) as pre-built object files containing data sections.
This script appends them to LINKFLAGS so they land in the final ELF.

US region only for now — when we add EU/JP/KR variants, gate the BCF here on a
build flag and pick the matching .o file.
"""

import os

Import("env", "projenv")

LIB_DIR = os.path.join(env.subst("$PROJECT_DIR"), "lib", "MorseWlan")

mbin_objects = [
    os.path.join(LIB_DIR, "src", "mm6108.mbin.o"),
    os.path.join(LIB_DIR, "src", "bcf_mf08651_us.mbin.o"),
]

# Only add objects that actually exist; missing ones surface as link errors,
# not silent corruption.
for obj in mbin_objects:
    if not os.path.isfile(obj):
        print("warning: mm-iot-esp32 blob missing: %s" % obj)

env.Append(LINKFLAGS=mbin_objects)
