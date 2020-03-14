# Meshtastic-esp32
This is the device side code for the [meshtastic.org](https://www.meshtastic.org) project.  

![Continuous Integration](https://github.com/meshtastic/Meshtastic-esp32/workflows/Continuous%20Integration/badge.svg)

Meshtastic is a project that lets you use
inexpensive GPS mesh radios as an extensible, super long battery life mesh GPS communicator.  These radios are great for hiking, skiing, paragliding - 
essentially any hobby where you don't have reliable internet access.  Each member of your private mesh can always see the location and distance of all other
members and any text messages sent to your group chat.

The radios automatically create a mesh to forward packets as needed, so everyone in the group can receive messages from even the furthest member.  The radios
will optionally work with your phone, but no phone is required.

Typical time between recharging the radios should be about eight days.

This project is currently early-alpha, but if you have questions please [join our discussion forum](https://meshtastic.discourse.group/).

This software is 100% open source and developed by a group of hobbyist experimenters.  No warranty is provided, if you'd like to improve it - we'd love your help.  Please post in the chat.  

## Supported hardware
We currently support three models of radios.  The [TTGO T-Beam](https://www.aliexpress.com/item/4000119152086.html), [TTGO LORA32](https://www.banggood.com/LILYGO-TTGO-LORA32-868Mhz-SX1276-ESP32-Oled-Display-bluetooth-WIFI-Lora-Development-Module-Board-p-1248652.html?cur_warehouse=UK) and the [Heltec LoRa 32](https://heltec.org/project/wifi-lora-32/).  Most users should buy the T-Beam and a 18650 battery (total cost less than $35).  Make 
sure to buy the frequency range which is legal for your country.  For the USA, you should buy the 915MHz version.  Getting a version that include a screen
is optional, but highly recommended.

See (meshtastic.org) for 3D printable cases.

## Installing the firmware
Prebuilt binaries for the supported radios is available in our [releases](https://github.com/meshtastic/Meshtastic-esp32/releases).  Your initial installation has to happen over USB from your Mac, Windows or Linux PC.   Once our software is installed, all future software updates happen over bluetooth from your phone.

The instructions currently require a few commmand lines, but it should be pretty straightforward.  Please post comments on our group chat if you have problems or successes.  Steps to install:

1. Purchase a radio (see above) with the correct frequencies for your country (915MHz for US or JP, 470MHz for CN, 433MHz and 870MHz for EU).
2. Install "pip".  Pip is the python package manager we use to get the esptool installer app.  Instructions [here](https://www.makeuseof.com/tag/install-pip-for-python/).
3. Run "pip install --upgrade esptool" to get esptool installed on your machine
4. Connect your radio to your USB port
5. Confirm that your device is talking to your PC by running "esptool.py chip_id".  The Heltec build also works on the TTGO LORA32 radio. You should see something like:
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
6. Install the correct firmware for your board with "esptool.py write_flash 0x10000 firmware-_board_-_country_.bin".  For instance "esptool.py write_flash 0x10000 release/firmware-HELTEC-US-0.0.3.bin". You should see something like this:
```
~/development/meshtastic/meshtastic-esp32$ esptool.py write_flash 0x10000 release/firmware-HELTEC-US-0.0.3.bin 
esptool.py v2.6
Found 2 serial ports
Serial port /dev/ttyUSB0
Connecting......
Detecting chip type... ESP32
Chip is ESP32D0WDQ6 (revision 1)
Features: WiFi, BT, Dual Core, 240MHz, VRef calibration in efuse, Coding Scheme None
MAC: 24:6f:28:b5:36:71
Uploading stub...
Running stub...
Stub running...
Configuring flash size...
Auto-detected Flash size: 8MB
Compressed 1184800 bytes to 652635...
Wrote 1184800 bytes (652635 compressed) at 0x00010000 in 57.6 seconds (effective 164.5 kbit/s)...
Hash of data verified.

Leaving...
Hard resetting via RTS pin...
```
7. The board will boot and show the Meshtastic logo.
8. Please post a comment on our chat so we know if these instructions worked for you ;-).  If you find bugs/have-questions post there also - we will be rapidly iterating over the next few weeks.

## Meshtastic Android app
The source code for the (optional) Meshtastic Android app is [here](https://github.com/meshtastic/Meshtastic-Android).  

Alpha test builds are current available by opting into our alpha test group.  See (www.meshtastic.org) for instructions.  

After our rate of change slows a bit, we will make beta builds available here (without needing to join the alphatest group):
[![Download at https://play.google.com/store/apps/details?id=com.geeksville.mesh](https://play.google.com/intl/en_us/badges/static/images/badges/en_badge_web_generic.png)](https://play.google.com/store/apps/details?id=com.geeksville.mesh&referrer=utm_source%3Dgithub%26utm_medium%3Desp32-readme%26utm_campaign%3Dmeshtastic-esp32%2520readme%26anid%3Dadmob&pcampaignid=pcampaignidMKT-Other-global-all-co-prtnr-py-PartBadge-Mar2515-1)

# Development

We'd love to have you join us on this merry little project.  Please see our [development documents](./docs/software/sw-design.md) and [join us in our discussion forum](https://meshtastic.discourse.group/).

# Credits

This project is run by volunteers.  Past contributors include:

* @astro-arphid: Added support for 433MHz radios in europe.
* @claesg: Various documentation fixes and 3D print enclosures
* @girtsf: So far our CI system, but soon lots of device improvements

# IMPORTANT DISCLAIMERS AND FAQ

For a listing of currently missing features and a FAQ click [here](docs/faq.md).