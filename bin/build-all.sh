#!/bin/bash

set -e

VERSION=0.0.3

COUNTRIES="US EU CN JP"

SRCMAP=.pio/build/esp32/output.map
SRCBIN=.pio/build/esp32/firmware.bin

for COUNTRY in $COUNTRIES; do 

    COMMONOPTS="-DAPP_VERSION=$VERSION -DHW_VERSION_$COUNTRY -Wall -Wextra -Wno-missing-field-initializers -Isrc -Os -Wl,-Map,.pio/build/esp32/output.map -DAXP_DEBUG_PORT=Serial"

    export PLATFORMIO_BUILD_FLAGS="-DT_BEAM_V10 $COMMONOPTS"
    echo "Building with $PLATFORMIO_BUILD_FLAGS"
    rm -f $SRCBIN $SRCMAP
    pio run # -v
    cp $SRCBIN release/firmware-TBEAM-$COUNTRY-$VERSION.bin
    cp $SRCMAP release/firmware-TBEAM-$COUNTRY-$VERSION.map

    export PLATFORMIO_BUILD_FLAGS="-DHELTEC_LORA32 $COMMONOPTS"
    rm -f $SRCBIN $SRCMAP
    pio run # -v
    cp $SRCBIN release/firmware-HELTEC-$COUNTRY-$VERSION.bin
    cp $SRCMAP release/firmware-HELTEC-$COUNTRY-$VERSION.map

done

zip release/firmware-$VERSION.zip release/firmware-*-$VERSION.bin

echo BUILT ALL