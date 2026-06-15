#!/usr/bin/env python3
# trunk-ignore-all(ruff/F821)
# trunk-ignore-all(flake8/F821): For SConstruct imports
#
# Post-link guard for the warm-node-store raw-flash region on nrf52840.
#
# The top 3 app-region flash pages below LittleFS (0xEA000-0xED000, reclaimed by
# whole-image LTO; LittleFS itself stays stock at 0xED000) are reserved for the WarmNodeStore record-ring
# (src/mesh/WarmNodeStore.h, 3-page record-ring). Our own linker script
# (src/platform/nrf52/nrf52840_s140_v7.ld) already caps the image at 0xEA000,
# but many boards (e.g. rak4631) link with the framework-default script whose
# FLASH region still ends at 0xED000 -- those would silently place code in the
# reserved pages if the image ever grew that large, and the first warm-store
# save would then brick the firmware. This guard turns that into a red build.
#
# Image flash end = __etext (end of code/rodata in flash) + sizeof(.data)
# (which is loaded at LMA __etext). Symbols come from the framework's
# nrf52_common.ld.
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
            "LENGTH in src/platform/nrf52/nrf52840_s140_v7.ld, and this guard).\n\n"
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
