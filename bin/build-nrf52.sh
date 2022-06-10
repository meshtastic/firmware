#!/usr/bin/env bash

set -e

VERSION=`bin/buildinfo.py long`
SHORT_VERSION=`bin/buildinfo.py short`

OUTDIR=release/

rm -f $OUTDIR/firmware*
rm -r $OUTDIR/* || true

# Make sure our submodules are current
git submodule update 

# Important to pull latest version of libs into all device flavors, otherwise some devices might be stale
platformio lib update 

echo "Building for $1 with $PLATFORMIO_BUILD_FLAGS"
rm -f .pio/build/$1/firmware.*

# The shell vars the build tool expects to find
export APP_VERSION=$VERSION

basename=firmware-$1-$VERSION

pio run --environment $1 # -v
SRCELF=.pio/build/$1/firmware.elf
cp $SRCELF $OUTDIR/$basename.elf

echo "Generating NRF52 uf2 file"
SRCHEX=.pio/build/$1/firmware.hex
bin/uf2conv.py $SRCHEX -c -o $OUTDIR/$basename.uf2 -f 0xADA52840

cp bin/device-install.* $OUTDIR
cp bin/device-update.* $OUTDIR
