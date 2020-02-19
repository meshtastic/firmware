set -e

VERSION=0.0.3

COUNTRY=US

rm .pio/build/esp32/firmware.bin
export PLATFORMIO_BUILD_FLAGS="-DT_BEAM_V10 -DAPP_VERSION=$VERSION"
pio run # -v
cp .pio/build/esp32/firmware.bin release/firmware-TBEAM-$COUNTRY.bin

rm .pio/build/esp32/firmware.bin
export PLATFORMIO_BUILD_FLAGS="-DHELTEC_LORA32 -DAPP_VERSION=$VERSION"
pio run # -v
cp .pio/build/esp32/firmware.bin release/firmware-HELTEC-$COUNTRY.bin