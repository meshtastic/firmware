set -e

VERSION=0.0.2

rm .pio/build/esp32/firmware.bin
export PLATFORMIO_BUILD_FLAGS="-DT_BEAM_V10 -DAPP_VERSION=$VERSION"
pio run # -v
cp .pio/build/esp32/firmware.bin release/firmware-TBEAM-US.bin

rm .pio/build/esp32/firmware.bin
export PLATFORMIO_BUILD_FLAGS="-DHELTEC_LORA32 -DAPP_VERSION=$VERSION"
pio run # -v
cp .pio/build/esp32/firmware.bin release/firmware-HELTEC-US.bin