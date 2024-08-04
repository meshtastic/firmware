#!/bin/bash
# adapted from the script linked in this very helpful article: https://enzolombardi.net/low-power-bluetooth-advertising-with-xiao-ble-and-platformio-e8e7d0da80d2

# source: https://gist.githubusercontent.com/turing-complete-labs/b3105ee653782183c54b4fdbe18f411f/raw/d86779ba7702775d3b79781da63d85442acd9de6/xiao_ble.sh
# download the core for arduino from seeedstudio. Softdevice 7.3.0, linker and variants folder are what we need
curl https://files.seeedstudio.com/arduino/core/nRF52840/Arduino_core_nRF52840.tar.bz2 -o arduino.core.1.0.0.tar.bz2
tar -xjf arduino.core.1.0.0.tar.bz2
rm arduino.core.1.0.0.tar.bz2

# copy the needed files
cp 1.0.0/cores/nRF5/linker/nrf52840_s140_v7.ld ~/.platformio/packages/framework-arduinoadafruitnrf52/cores/nRF5/linker
cp -r 1.0.0/cores/nRF5/nordic/softdevice/s140_nrf52_7.3.0_API ~/.platformio/packages/framework-arduinoadafruitnrf52/cores/nRF5/nordic/softdevice

rm -rf 1.0.0
echo done!
