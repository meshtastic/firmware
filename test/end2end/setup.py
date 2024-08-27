import time

import flash
import meshtastic
import meshtastic.serial_interface
from readprops import readProps

version = readProps("version.properties")["long"]


def setup_users_prefs(prefsLoc):
    with open(prefsLoc, "r") as file:
        filedata = file.read()
    filedata = filedata.replace(
        "// #define CONFIG_LORA_REGION_USERPREFS",
        "#define CONFIG_LORA_REGION_USERPREFS",
    )
    filedata = filedata.replace(
        "// #define LORACONFIG_CHANNEL_NUM_USERPREFS",
        "#define LORACONFIG_CHANNEL_NUM_USERPREFS",
    )
    filedata = filedata.replace(
        "// #define CHANNEL_0_PRECISION", "#define CHANNEL_0_PRECISION"
    )
    with open(prefsLoc, "w") as file:
        file.write(filedata)


def setup_device(port, pio_env, arch):
    interface = meshtastic.serial_interface.SerialInterface(port)
    try:
        interface.waitForConfig()
        if interface.metadata.firmware_version == version:
            print("Already at local ref version", version)
        else:
            print(
                "Device has version",
                interface.metadata.firmware_version,
                " updating to",
                version,
            )
            interface.close()
            time.sleep(1)
            flash_device(port, pio_env, arch)
            time.sleep(2)
    except:
        interface.close()
        time.sleep(1)
        flash_device(port, pio_env, arch)
        time.sleep(2)
        interface = meshtastic.serial_interface.SerialInterface(port)
        interface.waitForConfig()


def flash_device(port, pio_env, arch):
    if arch == "esp32":
        flash.flash_esp32(pio_env=pio_env, port=port)
    elif arch == "nrf52":
        flash.flash_nrf52(pio_env=pio_env, port=port)
