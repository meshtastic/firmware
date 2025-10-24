#!/usr/bin/env python3
# trunk-ignore-all(ruff/F821)
# trunk-ignore-all(flake8/F821): For SConstruct imports

import sys
from os.path import basename

Import("env")


# Custom HEX from ELF
# Convert hex to uf2 for nrf52
def nrf52_hex_to_uf2(source, target, env):
    hex_path = target[0].get_abspath()
    # When using merged hex, drop 'merged' from uf2 filename
    uf2_path = hex_path.replace(".merged.", ".")
    uf2_path = uf2_path.replace(".hex", ".uf2")
    env.Execute(
        env.VerboseAction(
            f'"{sys.executable}" ./bin/uf2conv.py "{hex_path}" -c -f 0xADA52840 -o "{uf2_path}"',
            f"Generating UF2 file from {basename(hex_path)}",
        )
    )


def nrf52_mergehex(source, target, env):
    hex_path = target[0].get_abspath()
    merged_hex_path = hex_path.replace(".hex", ".merged.hex")
    merge_with = None
    if "wio-sdk-wm1110" == str(env.get("PIOENV")):
        merge_with = env.subst("$PROJECT_DIR/bin/s140_nrf52_7.3.0_softdevice.hex")
    else:
        print("merge_with not defined for this target")

    if merge_with is not None:
        env.Execute(
            env.VerboseAction(
                f'"$PROJECT_DIR/bin/mergehex" -m "{hex_path}" "{merge_with}" -o "{merged_hex_path}"',
                "Merging HEX with SoftDevice",
            )
        )
        print(f'Merged file saved at "{basename(merged_hex_path)}"')
        nrf52_hex_to_uf2([hex_path, merge_with], [env.File(merged_hex_path)], env)


# if WM1110 target, merge hex with softdevice 7.3.0
if "wio-sdk-wm1110" == env.get("PIOENV"):
    env.AddPostAction("$BUILD_DIR/${PROGNAME}.hex", nrf52_mergehex)
else:
    env.AddPostAction("$BUILD_DIR/${PROGNAME}.hex", nrf52_hex_to_uf2)
