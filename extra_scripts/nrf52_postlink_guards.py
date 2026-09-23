#!/usr/bin/env python3
# trunk-ignore-all(ruff/F821)
# trunk-ignore-all(flake8/F821): For SConstruct imports
#
# Runs every nRF52 post-link guard registered in NRF52_POSTLINK_GUARDS (nrf52_lto.py,
# nrf52_warm_region.py) on every build, and before any firmware image is produced or uploaded.
#
# A post-action on the "buildprog" alias fired only when something under it rebuilt, so a no-change
# rerun after a failed guard printed SUCCESS over an image the guard had rejected. "buildprog" is
# also absent from `pio run -t upload`, which builds the firmware target directly. Here the guards
# are an AlwaysBuild alias over the ELF that every firmware image depends on, the same way the
# nordicnrf52 builder hangs "checkprogsize" off the firmware.
import os

Import("env")


def _run_postlink_guards(target, source, env):
    for guard in env.get("NRF52_POSTLINK_GUARDS", []):
        guard(source, target, env)  # a failing guard calls SCons Exit(1)


_guards = env.Alias(
    "nrf52_postlink_guards",
    "$BUILD_DIR/${PROGNAME}.elf",
    env.VerboseAction(_run_postlink_guards, "Checking nrf52 post-link guards"),
)
env.AlwaysBuild(_guards)

# Every nordicnrf52 upload protocol derives its firmware from one of these; an absent one is inert.
for _image in ("${PROGNAME}.hex", "${PROGNAME}.bin", "userfirmware.hex"):
    env.Depends(os.path.join("$BUILD_DIR", _image), _guards)
