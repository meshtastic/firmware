# Meshtastic-device

This is the device side code for the [meshtastic.org](https://www.meshtastic.org) project.

![Continuous Integration](https://github.com/meshtastic/Meshtastic-esp32/workflows/Continuous%20Integration/badge.svg)

Meshtastic™ is a project that lets you use
inexpensive GPS mesh radios as an extensible, super long battery life mesh GPS communicator. These radios are great for hiking, skiing, paragliding -
essentially any hobby where you don't have reliable internet access. Each member of your private mesh can always see the location and distance of all other
members and any text messages sent to your group chat.

The radios automatically create a mesh to forward packets as needed, so everyone in the group can receive messages from even the furthest member. The radios
will optionally work with your phone, but no phone is required.

Typical time between recharging the radios should be about eight days.

This project is is currently in beta-testing - if you have questions please [join our discussion forum](https://meshtastic.discourse.group/).

This software is 100% open source and developed by a group of hobbyist experimenters. No warranty is provided, if you'd like to improve it - we'd love your help. Please post in the chat.

## Supported hardware

We currently support three models of radios.

- TTGO T-Beam

  - [T-Beam V1.0 w/ NEO-6M - special Meshtastic version](https://www.aliexpress.com/item/4001178678568.html) (Includes built-in OLED display and they have **preinstalled** the meshtastic software)
  - [T-Beam V1.0 w/ NEO-M8N](https://www.aliexpress.com/item/33047631119.html) (slightly better GPS)
  - 3D printable cases
    - [T-Beam V0](https://www.thingiverse.com/thing:3773717)
    - [T-Beam V1](https://www.thingiverse.com/thing:3830711)

- [TTGO LORA32](https://www.aliexpress.com/item/4000211331316.html) - No GPS

  - 3D printable case
    - [TTGO LORA32 v1](https://www.thingiverse.com/thing:3385109)

- [Heltec LoRa 32](https://heltec.org/project/wifi-lora-32/) - No GPS
  - [3D Printable case](https://www.thingiverse.com/thing:3125854)

**Make sure to get the frequency for your country**

- US/JP/AU/NZ - 915MHz
- CN - 470MHz
- EU - 868MHz, 433MHz

Getting a version that includes a screen is optional, but highly recommended.

## Firmware Installation

Prebuilt binaries for the supported radios are available in our [releases](https://github.com/meshtastic/Meshtastic-esp32/releases). Your initial installation has to happen over USB from your Mac, Windows or Linux PC. Once our software is installed, all future software updates happen over bluetooth from your phone.

Be **very careful** to install the correct load for your board. In particular the popular 'T-BEAM' radio from TTGO is not called 'TTGO-Lora' (that is a different board). So don't install the 'TTGO-Lora' build on a TBEAM, it won't work correctly.

Please post comments on our [group chat](https://meshtastic.discourse.group/) if you have problems or successes.

### Installing from a GUI - Windows and Mac

1. Download and unzip the latest Meshtastic firmware [release](https://github.com/meshtastic/Meshtastic-esp32/releases).
2. Download [ESPHome Flasher](https://github.com/esphome/esphome-flasher/releases) (either x86-32bit Windows or x64-64 bit Windows).
3. Connect your radio to your USB port and open ESPHome Flasher.
4. If your board is not showing under Serial Port then you likely need to install the drivers for the CP210X serial chip. In Windows you can check by searching “Device Manager” and ensuring the device is shown under “Ports”.
5. If there is an error, download the drivers [here](https://www.silabs.com/products/development-tools/software/usb-to-uart-bridge-vcp-drivers), then unzip and run the Installer application.
6. In ESPHome Flasher, refresh the serial ports and select your board.
7. Browse to the previously downloaded firmware and select the correct firmware based on the board type, country and frequency.
8. Select Flash ESP.
9. Once complete, “Done! Flashing is complete!” will be shown.
10. Debug messages sent from the Meshtastic device can be viewed with a terminal program such as [PuTTY](https://www.putty.org/) (Windows only). Within PuTTY, click “Serial”, enter the “Serial line” com port (can be found at step 4), enter “Speed” as 921600, then click “Open”.

### Installing from a commandline

These instructions currently require a few commmand lines, but it should be pretty straightforward.

1. Install "pip". Pip is the python package manager we use to get the esptool installer app. Instructions [here](https://www.makeuseof.com/tag/install-pip-for-python/). If you are using OS-X, see these [special instructions](docs/software/install-OSX.md).
2. Run "pip install --upgrade esptool" to get esptool installed on your machine.
3. Connect your radio to your USB port.
4. Confirm that your device is talking to your PC by running "esptool.py chip_id". The Heltec build also works on the TTGO LORA32 radio. You should see something like:

```
mydir$ esptool.py chip_id
esptool.py v2.6
Found 2 serial ports
Serial port /dev/ttyUSB0
Connecting....
Detecting chip type... ESP32
Chip is ESP32D0WDQ6 (revision 1)
Features: WiFi, BT, Dual Core, 240MHz, VRef calibration in efuse, Coding Scheme None
MAC: 24:6f:28:b5:36:71
Uploading stub...
Running stub...
Stub running...
Warning: ESP32 has no Chip ID. Reading MAC instead.
MAC: 24:6f:28:b5:36:71
Hard resetting via RTS pin...
```

5. cd into the directory where the release zip file was expanded.
6. Install the correct firmware for your board with `device-install.sh firmware-_board_-_country_.bin`.
   - Example: `./device-install.sh firmware-HELTEC-US-0.0.3.bin`.
7. To update run `device-update.sh firmware-_board_-_country_.bin`
   - Example: `./device-update.sh firmware-HELTEC-US-0.0.3.bin`.

Note: If you have previously installed meshtastic, you don't need to run this full script instead just run `esptool.py --baud 921600 write_flash 0x10000 firmware-_board_-_country_-_version_.bin`. This will be faster, also all of your current preferences will be preserved.

You should see something like this:

```
kevinh@kevin-server:~/development/meshtastic/meshtastic-esp32/release/latest$ ./device-install.sh firmware-TBEAM-US-0.1.8.bin
Trying to flash firmware-TBEAM-US-0.1.8.bin, but first erasing and writing system information
esptool.py v2.6
Found 2 serial ports
Serial port /dev/ttyUSB0
Connecting........____
Detecting chip type... ESP32
Chip is ESP32D0WDQ6 (revision 1)
Features: WiFi, BT, Dual Core, 240MHz, VRef calibration in efuse, Coding Scheme None
MAC: 24:6f:28:b2:01:6c
Uploading stub...
Running stub...
Stub running...
Changing baud rate to 921600
Changed.
Erasing flash (this may take a while)...
Chip erase completed successfully in 6.1s
Hard resetting via RTS pin...
esptool.py v2.6
Found 2 serial ports
Serial port /dev/ttyUSB0
Connecting.......
Detecting chip type... ESP32
Chip is ESP32D0WDQ6 (revision 1)
Features: WiFi, BT, Dual Core, 240MHz, VRef calibration in efuse, Coding Scheme None
MAC: 24:6f:28:b2:01:6c
Uploading stub...
Running stub...
Stub running...
Changing baud rate to 921600
Changed.
Configuring flash size...
Auto-detected Flash size: 4MB
Flash params set to 0x0220
Compressed 61440 bytes to 11950...
Wrote 61440 bytes (11950 compressed) at 0x00001000 in 0.2 seconds (effective 3092.4 kbit/s)...
Hash of data verified.

Leaving...
Hard resetting via RTS pin...
esptool.py v2.6
Found 2 serial ports
Serial port /dev/ttyUSB0
Connecting.....
Detecting chip type... ESP32
Chip is ESP32D0WDQ6 (revision 1)
Features: WiFi, BT, Dual Core, 240MHz, VRef calibration in efuse, Coding Scheme None
MAC: 24:6f:28:b2:01:6c
Uploading stub...
Running stub...
Stub running...
Changing baud rate to 921600
Changed.
Configuring flash size...
Auto-detected Flash size: 4MB
Compressed 1223568 bytes to 678412...
Wrote 1223568 bytes (678412 compressed) at 0x00010000 in 10.7 seconds (effective 912.0 kbit/s)...
Hash of data verified.

Leaving...
Hard resetting via RTS pin...
```

7. The board will boot and show the Meshtastic logo.
8. Please post a comment on our chat so we know if these instructions worked for you ;-). If you find bugs/have-questions post there also - we will be rapidly iterating over the next few weeks.

# Meshtastic Android app

The companion (optional) Meshtastic Android app is [here](https://play.google.com/store/apps/details?id=com.geeksville.mesh&referrer=utm_source%3Dgithub-dev-readme). You can also download it on Google Play.

# Python API

We offer a [python API](https://github.com/meshtastic/Meshtastic-python) that makes it easy to use these devices to provide mesh networking for your custom projects.

# Development

We'd love to have you join us on this merry little project. Please see our [development documents](./docs/software/sw-design.md) and [join us in our discussion forum](https://meshtastic.discourse.group/).

# Credits

This project is run by volunteers. Past contributors include:

- @astro-arphid: Added support for 433MHz radios in europe.
- @claesg: Various documentation fixes and 3D print enclosures
- @girtsf: Lots of improvements
- @spattinson: Fixed interrupt handling for the AXP192 part

# IMPORTANT DISCLAIMERS AND FAQ

For a listing of currently missing features and a FAQ click [here](docs/faq.md).

Copyright 2019 Geeksville Industries, LLC. GPL V3 Licensed.
