#!/usr/bin/env python3
# trunk-ignore-all(ruff/F821)
# trunk-ignore-all(flake8/F821): For SConstruct imports
import sys
from os.path import join
from os import environ
import subprocess
import json
import shlex
import re
import time
from datetime import datetime

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
                      env.VerboseAction(f"\"{sys.executable}\" ./bin/uf2conv.py \"$BUILD_DIR/firmware.hex\" -c -f 0xADA52840 -o \"$BUILD_DIR/firmware.uf2\"",
                                        "Generating UF2 file"))

Import("projenv")

prefsLoc = projenv["PROJECT_DIR"] + "/version.properties"
verObj = readProps(prefsLoc)
print("Using meshtastic platformio-custom.py, firmware version " + verObj["long"] + " on " + env.get("PIOENV"))

# get repository owner if git is installed
try:
    r_owner = (
        subprocess.check_output(["git", "config", "--get", "remote.origin.url"])
        .decode("utf-8")
        .strip().split("/")
    )
    repo_owner = r_owner[-2] + "/" + r_owner[-1].replace(".git", "")
except subprocess.CalledProcessError:
    repo_owner = "unknown"

pref_flags = {}

def contribute_flag(key, value):
    if value:
        pref_flags[key] = f"-D{key}={value}"
    else:
        pref_flags[key] = f"-D{key}"


# General options that are passed to the C and C++ compilers
# Calculate unix epoch for current day (midnight)
current_date = datetime.now().replace(hour=0, minute=0, second=0, microsecond=0)
build_epoch = int(current_date.timestamp())

contribute_flag("APP_VERSION", verObj["long"])
contribute_flag("APP_VERSION_SHORT", verObj["short"])
contribute_flag("APP_ENV", env.get("PIOENV"))
contribute_flag("APP_REPO", repo_owner)
contribute_flag("BUILD_EPOCH", f"{build_epoch}")

# userPrefs from file
jsonLoc = env["PROJECT_DIR"] + "/userPrefs.jsonc"
with open(jsonLoc) as f:
    jsonStr = re.sub("//.*","", f.read(), flags=re.MULTILINE)
    userPrefs = json.loads(jsonStr)

for pref_key, pref_value in userPrefs.items():

    if (
        pref_value.startswith("{") or
        pref_value.lstrip("-").replace(".", "").isdigit() or
        pref_value == "true" or pref_value == "false" or 
        pref_value.startswith("meshtastic_")
    ):
        contribute_flag(pref_key, pref_value)

    else:
        # If the value is a string, we need to wrap it in quotes
        contribute_flag(pref_key, env.StringifyMacro(pref_value))


# Additional prefs from env
# Sample: $ PREFS="OLED_RU" ./bin/build-firmware.sh heltec-mesh-node-t114 nrf52
for pref in shlex.split(environ.get("PREFS", "")):
    key, sep, value = pref.partition('=')
    contribute_flag(key, value)


# All prefs are gathered now, proceed
flags = list(pref_flags.values())

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

# Get the display resolution from macros
def get_display_resolution(build_flags):
    # Check "DISPLAY_SIZE" to determine the screen resolution
    for flag in build_flags:
        if isinstance(flag, tuple) and flag[0] == "DISPLAY_SIZE":
            screen_width, screen_height = map(int, flag[1].split("x"))
            return screen_width, screen_height
    print("No screen resolution defined in build_flags. Please define DISPLAY_SIZE.")
    exit(1)

def load_boot_logo(source, target, env):
    build_flags = env.get("CPPDEFINES", [])
    logo_w, logo_h = get_display_resolution(build_flags)
    print(f"TFT build with {logo_w}x{logo_h} resolution detected")

    # Load the boot logo from `branding/logo_<width>x<height>.png` if it exists
    source_path = join(env["PROJECT_DIR"], "branding", f"logo_{logo_w}x{logo_h}.png")
    dest_dir = join(env["PROJECT_DIR"], "data", "boot")
    dest_path = join(dest_dir, "logo.png")
    if env.File(source_path).exists():
        print(f"Loading boot logo from {source_path}")
        # Prepare the destination
        env.Execute(f"mkdir -p {dest_dir} && rm -f {dest_path}")
        # Copy the logo to the `data/boot` directory
        env.Execute(f"cp {source_path} {dest_path}")

# Load the boot logo on TFT builds
if ("HAS_TFT", 1) in env.get("CPPDEFINES", []):
    env.AddPreAction('$BUILD_DIR/littlefs.bin', load_boot_logo)
