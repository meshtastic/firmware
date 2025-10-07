#!/usr/bin/env python3
# trunk-ignore-all(ruff/F821)
# trunk-ignore-all(flake8/F821): For SConstruct imports

import sys

Import("env")


# Custom HEX from ELF
# Convert hex to uf2 for nrf52
def nrf52_hex_to_uf2(source, target, env):
    hex_path = target[0].get_abspath()
    uf2_path = hex_path.replace(".hex", ".uf2")
    env.Execute(
        env.VerboseAction(
            f'"{sys.executable}" ./bin/uf2conv.py "{hex_path}" -c -f 0xADA52840 -o "{uf2_path}"',
            "Generating UF2 file",
        )
    )


env.AddPostAction("$BUILD_DIR/${PROGNAME}.hex", nrf52_hex_to_uf2)
