# esp32-arduino build instructions

We build our own custom version of esp32-arduino, in order to get some fixes we've made but haven't yet been merged in master.

These are a set of currently unformatted notes on how to build and install them. Most developers should not care about this, because
you'll automatically get our fixed libraries.

```
  last EDF release in arduino is: https://github.com/espressif/arduino-esp32/commit/1977370e6fc069e93ffd8818798fbfda27ae7d99
  IDF release/v3.3 46b12a560
  IDF release/v3.3 367c3c09c
  https://docs.espressif.com/projects/esp-idf/en/release-v3.3/get-started/linux-setup.html
  kevinh@kevin-server:~/development/meshtastic/esp32-arduino-lib-builder\$ python /home/kevinh/development/meshtastic/esp32-arduino-lib-builder/esp-idf/components/esptool*py/esptool/esptool.py --chip esp32 --port /dev/ttyUSB0 --baud 921600 --before default_reset --after hard_reset write_flash -z --flash_mode dout --flash_freq 40m --flash_size detect 0x1000 /home/kevinh/development/meshtastic/esp32-arduino-lib-builder/build/bootloader/bootloader.bin
  cp -a out/tools/sdk/* components/arduino/tools/sdk
  cp -ar components/arduino/* ~/.platformio/packages/framework-arduinoespressif32@src-fba9d33740f719f712e9f8b07da6ea13/
```
