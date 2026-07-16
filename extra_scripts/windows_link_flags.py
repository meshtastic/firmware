#!/usr/bin/env python3
# trunk-ignore-all(ruff/F821)
# trunk-ignore-all(flake8/F821): For SConstruct imports
#
# PlatformIO routes build_flags to the compile step only, so the static link for
# [env:native-windows] has to be appended to LINKFLAGS here, as
# extra_scripts/wasm_link_flags.py does for [env:native-wasm].
Import("env")

if env["PIOENV"].startswith("native-windows"):
    env.Append(
        LINKFLAGS=[
            "-static",
            "-static-libgcc",
            "-static-libstdc++",
        ]
    )
