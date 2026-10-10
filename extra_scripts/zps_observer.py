#!/usr/bin/env python3
# trunk-ignore-all(ruff/F821)
# trunk-ignore-all(flake8/F821): For SConstruct imports
import re

Import("env")

# The ZPS module scans BLE through the NimBLE *host* API ble_gap_disc(), which the
# ESP-IDF only compiles when CONFIG_BT_NIMBLE_ROLE_OBSERVER=y. The base esp32 config
# disables the observer role to save flash (see variants/esp32/esp32-common.ini), so
# re-enable it ONLY when the ZPS module is actually built. This keeps a single flag
# (-DMESHTASTIC_EXCLUDE_ZPS) driving both the C++ module and the IDF sdkconfig.
#
# This runs as a pre: script, before the arduino framework builder reads
# custom_sdkconfig (PlatformIO core runs pre-scripts before $BUILD_SCRIPT). Appending
# to custom_sdkconfig changes its hash and triggers the framework's IDF rebuild path,
# but only for ZPS-enabled envs; for every normal build this is a no-op.
flags = env.GetProjectOption("build_flags", "")
if isinstance(flags, (list, tuple)):
    flags = " ".join(flags)

# Mirror the C semantics of `#if !MESHTASTIC_EXCLUDE_ZPS`:
#   flag absent          -> module enabled
#   -DMESHTASTIC_EXCLUDE_ZPS=0 -> module enabled
#   -DMESHTASTIC_EXCLUDE_ZPS or =1 (or anything else) -> module excluded
match = re.search(r"\bMESHTASTIC_EXCLUDE_ZPS\b(?:=(\S+))?", flags)
zps_enabled = (match is None) or (match.group(1) == "0")

section = "env:" + env["PIOENV"]
config = env.GetProjectConfig()
if zps_enabled and config.has_option(section, "custom_sdkconfig"):
    sdkconfig = env.GetProjectOption("custom_sdkconfig")
    if "CONFIG_BT_NIMBLE_ROLE_OBSERVER" not in sdkconfig:
        config.set(
            section,
            "custom_sdkconfig",
            sdkconfig.rstrip("\n") + "\n  CONFIG_BT_NIMBLE_ROLE_OBSERVER=y\n",
        )
        print("[ZPS] module enabled -> CONFIG_BT_NIMBLE_ROLE_OBSERVER=y (IDF rebuild)")
