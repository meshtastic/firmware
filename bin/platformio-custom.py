#!/usr/bin/env python3
# trunk-ignore-all(ruff/F821)
# trunk-ignore-all(flake8/F821): For SConstruct imports
import sys
from os.path import join
import json
import re

from readprops import readProps

Import("env")
platform = env.PioPlatform()


def esp32_create_combined_bin(source, target, env):
    # this sub is borrowed from ESPEasy build toolchain. It's licensed under GPL V3
    # https://github.com/letscontrolit/ESPEasy/blob/mega/tools/pio/post_esp32.py
    print("Generating combined binary for serial flashing")

    app_offset = 0x10000

    new_file_name = env.subst("$BUILD_DIR/${PROGNAME}.factory.bin")
    sections = env.subst(env.get("FLASH_EXTRA_IMAGES"))
    firmware_name = env.subst("$BUILD_DIR/${PROGNAME}.bin")
    chip = env.get("BOARD_MCU")
    flash_size = env.BoardConfig().get("upload.flash_size")
    flash_freq = env.BoardConfig().get("build.f_flash", "40m")
    flash_freq = flash_freq.replace("000000L", "m")
    flash_mode = env.BoardConfig().get("build.flash_mode", "dio")
    memory_type = env.BoardConfig().get("build.arduino.memory_type", "qio_qspi")
    if flash_mode == "qio" or flash_mode == "qout":
        flash_mode = "dio"
    if memory_type == "opi_opi" or memory_type == "opi_qspi":
        flash_mode = "dout"
    cmd = [
        "--chip",
        chip,
        "merge_bin",
        "-o",
        new_file_name,
        "--flash_mode",
        flash_mode,
        "--flash_freq",
        flash_freq,
        "--flash_size",
        flash_size,
    ]

    print("    Offset | File")
    for section in sections:
        sect_adr, sect_file = section.split(" ", 1)
        print(f" -  {sect_adr} | {sect_file}")
        cmd += [sect_adr, sect_file]

    print(f" - {hex(app_offset)} | {firmware_name}")
    cmd += [hex(app_offset), firmware_name]

    print("Using esptool.py arguments: %s" % " ".join(cmd))

    esptool.main(cmd)


if platform.name == "espressif32":
    sys.path.append(join(platform.get_package_dir("tool-esptoolpy")))
    import esptool

    env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", esp32_create_combined_bin)

    esp32_kind = env.GetProjectOption("custom_esp32_kind")
    if esp32_kind == "esp32":
        # Free up some IRAM by removing auxiliary SPI flash chip drivers.
        # Wrapped stub symbols are defined in src/platform/esp32/iram-quirk.c.
        env.Append(
            LINKFLAGS=[
                "-Wl,--wrap=esp_flash_chip_gd",
                "-Wl,--wrap=esp_flash_chip_issi",
                "-Wl,--wrap=esp_flash_chip_winbond",
            ]
        )
    else:
        # For newer ESP32 targets, using newlib nano works better.
        env.Append(LINKFLAGS=["--specs=nano.specs", "-u", "_printf_float"])

if platform.name == "nordicnrf52":
    env.AddPostAction("$BUILD_DIR/${PROGNAME}.hex",
                      env.VerboseAction(f"\"{sys.executable}\" ./bin/uf2conv.py $BUILD_DIR/firmware.hex -c -f 0xADA52840 -o $BUILD_DIR/firmware.uf2",
                                        "Generating UF2 file"))

Import("projenv")

prefsLoc = projenv["PROJECT_DIR"] + "/version.properties"
verObj = readProps(prefsLoc)
print("Using meshtastic platformio-custom.py, firmware version " + verObj["long"] + " on " + env.get("PIOENV"))

jsonLoc = env["PROJECT_DIR"] + "/userPrefs.jsonc"
with open(jsonLoc) as f:
    jsonStr = re.sub("//.*","", f.read(), flags=re.MULTILINE)
    userPrefs = json.loads(jsonStr)

pref_flags = []
# Pre-process the userPrefs
for pref in userPrefs:
    if userPrefs[pref].startswith("{"):
        pref_flags.append("-D" + pref + "=" + userPrefs[pref])
    elif userPrefs[pref].lstrip("-").replace(".", "").isdigit():
        pref_flags.append("-D" + pref + "=" + userPrefs[pref])
    elif userPrefs[pref] == "true" or userPrefs[pref] == "false":
        pref_flags.append("-D" + pref + "=" + userPrefs[pref])
    elif userPrefs[pref].startswith("meshtastic_"):
        pref_flags.append("-D" + pref + "=" + userPrefs[pref])
    # If the value is a string, we need to wrap it in quotes
    else:
        pref_flags.append("-D" + pref + "=" + env.StringifyMacro(userPrefs[pref]) + "")

# General options that are passed to the C and C++ compilers
flags = [
        "-DAPP_VERSION=" + verObj["long"],
        "-DAPP_VERSION_SHORT=" + verObj["short"],
        "-DAPP_ENV=" + env.get("PIOENV"),
    ] + pref_flags

print ("Using flags:")
for flag in flags:
    print(flag)
    
projenv.Append(
    CCFLAGS=flags,
)

for lb in env.GetLibBuilders():
    if lb.name == "meshtastic-device-ui":
        lb.env.Append(CPPDEFINES=[("APP_VERSION", verObj["long"])])
        break
