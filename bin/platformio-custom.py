import subprocess
import configparser
import traceback
import sys
from os.path import join
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
    flash_freq = env.BoardConfig().get("build.f_flash", '40m')
    flash_freq = flash_freq.replace('000000L', 'm')
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

    print('Using esptool.py arguments: %s' % ' '.join(cmd))

    esptool.main(cmd)

if (platform.name == "espressif32"):
    sys.path.append(join(platform.get_package_dir("tool-esptoolpy")))
    import esptool
    env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", esp32_create_combined_bin)    

Import("projenv")

prefsLoc = projenv["PROJECT_DIR"] + "/version.properties"
verObj = readProps(prefsLoc)
print("Using meshtastic platformio-custom.py, firmware version " + verObj['long'])

# General options that are passed to the C and C++ compilers
projenv.Append(CCFLAGS=[
    "-DAPP_VERSION=" + verObj['long'],
    "-DAPP_VERSION_SHORT=" + verObj['short']    
])
