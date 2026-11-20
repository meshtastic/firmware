#!/usr/bin/env python3
# trunk-ignore-all(ruff/F821)
# trunk-ignore-all(flake8/F821): For SConstruct imports

import sys
from os.path import basename

Import("env")

# UF2 family of the nRF54LM20 bootloader. microsoft/uf2 has no nRF54 family, so this one was
# picked at random; the bootloader also takes its board ID (VID << 16 | UF2 PID).
NRF54LM20_UF2_FAMILY = "0x9EEA6BF9"


# Convert the application hex to uf2. On nRF54L ${PROGNAME}.hex is the whole image for SWD
# (SoftDevice + bootloader + application), and the bootloader refuses UF2 blocks outside the
# application, so the uf2 is built from userfirmware.hex.
def nrf54_app_hex_to_uf2(source, target, env):
    hex_path = target[0].get_abspath()
    uf2_path = env.subst("$BUILD_DIR/${PROGNAME}.uf2")
    env.Execute(
        env.VerboseAction(
            f'"{sys.executable}" ./bin/uf2conv.py "{hex_path}" -c -f {NRF54LM20_UF2_FAMILY} -o "{uf2_path}"',
            f"Generating UF2 file from {basename(hex_path)}",
        )
    )


env.AddPostAction("$BUILD_DIR/userfirmware.hex", nrf54_app_hex_to_uf2)
