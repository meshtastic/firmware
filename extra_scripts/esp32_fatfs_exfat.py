#!/usr/bin/env python3
# trunk-ignore-all(ruff/F821)
# trunk-ignore-all(flake8/F821): For SConstruct imports
import re
from os.path import exists, join

Import("env")

# ---------------------------------------------------------------------------
# exFAT for the IDF FatFs component.
#
# ESP-IDF exposes no Kconfig symbol for exFAT (checked 5.5.x and master):
# components/fatfs/src/ffconf.h hardcodes "#define FF_FS_EXFAT 0", so
# CONFIG_FATFS_FS_EXFAT=y in custom_sdkconfig is silently dropped by kconfgen
# and an exFAT card fails to mount with ESP_FAIL from esp_vfs_fat_sdmmc_mount.
# This script turns that inert symbol into a real ffconf.h patch.
#
# Two headers must agree, because FF_FS_EXFAT changes the FATFS/FIL/DIR layout
# in ff.h:
#   1. framework-espidf - compiled into libfatfs.a by the HybridCompile
#      IDF-libs rebuild (arduino.py -> espidf.py). Patched in the pre: pass.
#   2. framework-arduinoespressif32-libs/<chip>/include - used when the
#      application and the Arduino SD_MMC library are compiled. espidf.py's
#      idf_lib_copy() copies back archives and sdkconfig.h but no headers, and
#      a custom_sdkconfig hash change wipes the whole package and reinstalls
#      it, so this copy is patched again in the post: pass.
#
# framework-espidf is shared by every ESP32 env while this script is registered
# only on the SDIO variants, so the patch there is undone once the build is
# done: an env that never asked for exFAT must not rebuild its IDF libs against
# a header the rest of its build does not see. The per-chip -libs copy stays
# patched, matching the libfatfs.a it was built with.
#
# The libs rebuild is keyed on the md5 of custom_sdkconfig, which the patch
# state does not otherwise reach, so the state is appended as a comment line
# (inert in kconfig). Keep the marker lowercase: arduino.py greps
# custom_sdkconfig for "PSRAM" and "CONFIG_SPIRAM=y".
# ---------------------------------------------------------------------------

SWITCH = "CONFIG_FATFS_FS_EXFAT=y"
MARKER_KEY = "meshtastic_fatfs_exfat"
PATTERN = re.compile(r"^(#define\s+FF_FS_EXFAT\s+)([01])", re.MULTILINE)


def wants_exfat(env):
    config = env.GetProjectConfig()
    section = "env:" + env["PIOENV"]
    if not config.has_option(section, "custom_sdkconfig"):
        return False
    return any(
        line.strip() == SWITCH
        for line in env.GetProjectOption("custom_sdkconfig").splitlines()
    )


def ffconf_paths(env, phase):
    platform = env.PioPlatform()
    board = env.BoardConfig()
    chip = board.get("build.chip_variant", "").lower() or board.get(
        "build.mcu", "esp32"
    )

    paths = []
    if phase == "pre":
        idf = idf_ffconf(env)
        if idf:
            paths.append(idf)
    libs_dir = platform.get_package_dir("framework-arduinoespressif32-libs")
    if libs_dir:
        paths.append(join(libs_dir, chip, "include", "fatfs", "src", "ffconf.h"))
    return [p for p in paths if exists(p)]


def idf_ffconf(env):
    idf_dir = env.PioPlatform().get_package_dir("framework-espidf")
    return join(idf_dir, "components", "fatfs", "src", "ffconf.h") if idf_dir else None


def set_ff_fs_exfat(path, enable):
    with open(path) as src:
        content = src.read()
    patched = PATTERN.sub(r"\g<1>%d" % (1 if enable else 0), content, count=1)
    if patched == content:
        return False
    with open(path, "w") as dst:
        dst.write(patched)
    print("*** FF_FS_EXFAT=%d: %s ***" % (1 if enable else 0, path))
    return True


def tag_sdkconfig_state(env, enable):
    config = env.GetProjectConfig()
    section = "env:" + env["PIOENV"]
    if not config.has_option(section, "custom_sdkconfig"):
        return
    current = env.GetProjectOption("custom_sdkconfig")
    if MARKER_KEY in current:
        return
    marker = "# %s: %d" % (MARKER_KEY, 1 if enable else 0)
    config.set(section, "custom_sdkconfig", current.rstrip("\n") + "\n" + marker)


phase = "post" if env.get("MESHTASTIC_EXFAT_PATCHED") else "pre"
enable = wants_exfat(env)

for path in ffconf_paths(env, phase):
    set_ff_fs_exfat(path, enable)

if phase == "pre":
    # revert as well as enable, so an env without the switch never links a
    # FatFs built with a different struct layout than the headers it compiles against
    tag_sdkconfig_state(env, enable)
    env["MESHTASTIC_EXFAT_PATCHED"] = True

    if enable:

        def restore_idf_ffconf(target, source, env):
            idf = idf_ffconf(env)
            if idf and exists(idf):
                set_ff_fs_exfat(idf, False)

        # the IDF libs are compiled during the build phase, so this is the first
        # point at which the shared package can be handed back unpatched
        env.AddPostAction("checkprogsize", restore_idf_ffconf)
