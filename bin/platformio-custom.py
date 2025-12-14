#!/usr/bin/env python3
# trunk-ignore-all(ruff/F821)
# trunk-ignore-all(flake8/F821): For SConstruct imports
import sys
from os import environ
from os.path import join, basename, isfile
import subprocess
import json
import shlex
import re
from datetime import datetime

from readprops import readProps

Import("env")
platform = env.PioPlatform()
progname = env.get("PROGNAME")
lfsbin = f"{progname.replace('firmware-', 'littlefs-')}.bin"

def manifest_gather(source, target, env):
    out = []
    check_paths = [
        progname,
        f"{progname}.elf",
        f"{progname}.bin",
        f"{progname}.factory.bin",
        f"{progname}.hex",
        f"{progname}.merged.hex",
        f"{progname}.uf2",
        f"{progname}.factory.uf2",
        f"{progname}.zip",
        lfsbin
    ]
    for p in check_paths:
        f = env.File(env.subst(f"$BUILD_DIR/{p}"))
        if f.exists():
            d = {
                "name": p,
                "md5": f.get_content_hash(), # Returns MD5 hash
                "bytes": f.get_size() # Returns file size in bytes
            }
            out.append(d)
            print(d)
    manifest_write(out, env)

def manifest_write(files, env):
    manifest = {
        "version": verObj["long"],
        "build_epoch": build_epoch,
        "board": env.get("PIOENV"),
        "mcu": env.get("BOARD_MCU"),
        "repo": repo_owner,
        "files": files,
        "part": None,
        "has_mui": False,
        "has_inkhud": False,
    }
    # Get partition table (generated in esp32_pre.py) if it exists
    if env.get("custom_mtjson_part"):
        # custom_mtjson_part is a JSON string, convert it back to a dict
        pj = json.loads(env.get("custom_mtjson_part"))
        manifest["part"] = pj
    # Enable has_mui for TFT builds
    if ("HAS_TFT", 1) in env.get("CPPDEFINES", []):
        manifest["has_mui"] = True
    if "MESHTASTIC_INCLUDE_INKHUD" in env.get("CPPDEFINES", []):
        manifest["has_inkhud"] = True

    # Write the manifest to the build directory
    with open(env.subst("$BUILD_DIR/${PROGNAME}.mt.json"), "w") as f:
        json.dump(manifest, f, indent=2)

Import("projenv")

prefsLoc = projenv["PROJECT_DIR"] + "/version.properties"
verObj = readProps(prefsLoc)
print(f"Using meshtastic platformio-custom.py, firmware version {verObj['long']} on {env.get('PIOENV')}")

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

print("Using flags:")
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
    env.AddPreAction(f"$BUILD_DIR/{lfsbin}", load_boot_logo)

mtjson_deps = ["buildprog"]
if platform.name == "espressif32":
    # Build littlefs image as part of mtjson target
    # Equivalent to `pio run -t buildfs`
    target_lfs = env.DataToBin(
        join("$BUILD_DIR", "${ESP32_FS_IMAGE_NAME}"), "$PROJECT_DATA_DIR"
    )
    mtjson_deps.append(target_lfs)

env.AddCustomTarget(
    name="mtjson",
    dependencies=mtjson_deps,
    actions=[manifest_gather],
    title="Meshtastic Manifest",
    description="Generating Meshtastic manifest JSON + Checksums",
    always_build=False,
)
