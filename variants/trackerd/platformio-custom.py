# trunk-ignore-all(ruff/F821)
# trunk-ignore-all(flake8/F821): For SConstruct imports
import sys
from os.path import join

Import("env")
Import("projenv")

sys.path.append(projenv["PROJECT_DIR"] + "/bin")

from readprops import readProps

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
    flash_size = env.BoardConfig().get("upload.flash_size", "4MB")
    flash_mode = env["__get_board_flash_mode"](env)
    flash_freq = env["__get_board_f_flash"](env)

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


prefsLoc = projenv["PROJECT_DIR"] + "/version.properties"
verObj = readProps(prefsLoc)
print("Using meshtastic platformio-custom.py, firmware version " + verObj["long"])


if platform.name == "espressif32":
    sys.path.append(join(platform.get_package_dir("tool-esptoolpy")))
    import esptool

    env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", esp32_create_combined_bin)


# General options that are passed to the C and C++ compilers
projenv.Append(
    CCFLAGS=[
        "-DAPP_VERSION=" + verObj["long"],
        "-DAPP_VERSION_SHORT=" + verObj["short"],
    ]
)
