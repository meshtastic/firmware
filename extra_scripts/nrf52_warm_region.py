#!/usr/bin/env python3
# trunk-ignore-all(ruff/F821)
# trunk-ignore-all(flake8/F821): For SConstruct imports
#
# Post-link guard for the warm-node-store raw-flash region on nRF52840.
#
# The 3 app-region pages below LittleFS (0xEA000-0xED000, reclaimed by whole-image
# LTO) are reserved for the WarmNodeStore record-ring (see WarmNodeStore.h). Our
# linker scripts (nrf52840_s140_v6.ld and nrf52840_s140_v7.ld) cap the image at
# 0xEA000, but boards on the framework-default script (FLASH ending at 0xED000) could
# silently place code in those pages - the first warm-store save would then brick the
# device. This turns that into a build failure.
#
# Image flash end = __etext + sizeof(.data) (loaded at LMA __etext); symbols from
# the framework's nrf52_common.ld.
import os

Import("env")

WARM_REGION_BASE = 0xEA000  # keep in sync with WARM_FLASH_REGION_BASE in WarmNodeStore.h (3 x 4 KB record-ring)

_tc = env.PioPlatform().get_package_dir("toolchain-gccarmnoneeabi") or ""
_NM = os.path.join(_tc, "bin", "arm-none-eabi-nm")
if not os.path.isfile(_NM):
    _NM = "arm-none-eabi-nm"  # fall back to PATH


def _assert_warm_region_clear(source, target, env):
    import subprocess
    import sys

    try:
        elf = env.subst("$BUILD_DIR/${PROGNAME}.elf")
        out = subprocess.check_output([_NM, elf], universal_newlines=True)
    except Exception as exc:  # tooling hiccup: warn loudly, don't wedge the build
        print("nrf52_warm_region: WARNING - guard skipped (nm failed: %s)" % exc)
        return

    syms = {}
    for line in out.split("\n"):
        f = line.split()
        if len(f) >= 3 and f[-1] in ("__etext", "__data_start__", "__data_end__"):
            syms[f[-1]] = int(f[0], 16)
    if len(syms) != 3:
        print("nrf52_warm_region: WARNING - guard skipped (linker symbols not found)")
        return

    flash_end = syms["__etext"] + (syms["__data_end__"] - syms["__data_start__"])
    if flash_end > WARM_REGION_BASE:
        sys.stderr.write(
            "\n*** nrf52 warm-region guard: image ends at 0x%X, past the reserved "
            "warm-store region at 0x%X ***\n"
            "The 12 KB region at 0xEA000 holds the WarmNodeStore record-ring; a warm-store\n"
            "save would overwrite this firmware's tail. Shrink the image, or shrink/move\n"
            "the region (WARM_FLASH_REGION_BASE in src/mesh/WarmNodeStore.h, the FLASH\n"
            "LENGTH in src/platform/nrf52/nrf52840_s140_v6.ld and _v7.ld, and this guard).\n\n"
            % (flash_end, WARM_REGION_BASE)
        )
        from SCons.Script import Exit

        Exit(1)
    print(
        "nrf52_warm_region: guard OK -- image ends at 0x%X, %d KB clear of the warm region"
        % (flash_end, (WARM_REGION_BASE - flash_end) // 1024)
    )


# Attach to the phony "buildprog" alias (not the .elf node) so the guard runs
# on incremental relinks too -- same reasoning as nrf52_lto.py's guard.
env.AddPostAction("buildprog", _assert_warm_region_clear)
