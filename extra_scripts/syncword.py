# trunk-ignore-all(ruff/F821)
# trunk-ignore-all(flake8/F821): For SConstruct imports
"""
Custom PlatformIO target: privatesync

Usage:
    pio run -e native -t privatesync

Adds -DMESHTASTIC_SYNCWORD_0x12=1 to the build so the firmware uses
syncWord=0x12 instead of the default 0x2b. Required when pairing with 
some devices (with FRAME_SYNCH registers are 5-bit signed; 0x2b
overflows). Nodes built with this flag cannot communicate with stock 0x2b nodes.
"""

Import("env")

import SCons.Script  # noqa: E402 (available after Import)

if "privatesync" in SCons.Script.COMMAND_LINE_TARGETS:
    env.Append(CPPDEFINES=["MESHTASTIC_SYNCWORD_0x12=1"])
    print("privatesync: syncWord=0x12 enabled (SX1302 gateway mode)")

env.AddCustomTarget(
    name="privatesync",
    dependencies=["$BUILD_DIR/${PROGNAME}${PROGSUFFIX}"],
    actions=None,
    title="Private Sync Word (0x12)",
    description="Build with syncWord=0x12 for most devices  compatibility",
)
