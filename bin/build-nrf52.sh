#!/usr/bin/env bash

set -e

VERSION=$(bin/buildinfo.py long)
SHORT_VERSION=$(bin/buildinfo.py short)

OUTDIR=release/

rm -f $OUTDIR/firmware*
rm -r $OUTDIR/* || true

# Important to pull latest version of libs into all device flavors, otherwise some devices might be stale
platformio pkg update

echo "Building for $1 with $PLATFORMIO_BUILD_FLAGS"
rm -f .pio/build/$1/firmware.*

# The shell vars the build tool expects to find
export APP_VERSION=$VERSION

basename=firmware-$1-$VERSION

pio run --environment $1 # -v
SRCELF=.pio/build/$1/firmware.elf
cp $SRCELF $OUTDIR/$basename.elf

echo "Generating NRF52 dfu file"
DFUPKG=.pio/build/$1/firmware.zip
cp $DFUPKG $OUTDIR/$basename-ota.zip

echo "Generating NRF52 uf2 file"
SRCHEX=.pio/build/$1/firmware.hex

# if WM1110 target, merge hex with softdevice 7.3.0
if (echo $1 | grep -q "wio-sdk-wm1110"); then
	echo "Merging with softdevice"
	bin/mergehex -m bin/s140_nrf52_7.3.0_softdevice.hex $SRCHEX -o .pio/build/$1/$basename.hex
	SRCHEX=.pio/build/$1/$basename.hex
	bin/uf2conv.py $SRCHEX -c -o $OUTDIR/$basename.uf2 -f 0xADA52840
	cp $SRCHEX $OUTDIR
	cp bin/*.uf2 $OUTDIR
else
	bin/uf2conv.py $SRCHEX -c -o $OUTDIR/$basename.uf2 -f 0xADA52840
	cp bin/device-install.* $OUTDIR
	cp bin/device-update.* $OUTDIR
	cp bin/*.uf2 $OUTDIR
fi
