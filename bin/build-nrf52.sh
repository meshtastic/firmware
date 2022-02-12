#!/usr/bin/env bash

set -e

VERSION=`bin/buildinfo.py long`
SHORT_VERSION=`bin/buildinfo.py short`

OUTDIR=release/latest

ARCHIVEDIR=release/archive 

rm -f $OUTDIR/firmware*

mkdir -p $OUTDIR/bins $ARCHIVEDIR
rm -r $OUTDIR/bins/* || true
mkdir -p $OUTDIR/bins/universal $OUTDIR/elfs/universal

# Make sure our submodules are current
git submodule update 

# Important to pull latest version of libs into all device flavors, otherwise some devices might be stale
platformio lib update 

echo "Building for $1 with $PLATFORMIO_BUILD_FLAGS"
rm -f .pio/build/$1/firmware.*

# The shell vars the build tool expects to find
export APP_VERSION=$VERSION

# Are we building a universal/regionless rom?
export HW_VERSION="1.0"
basename=universal/firmware-$1-$VERSION

pio run --environment $1 # -v
SRCELF=.pio/build/$1/firmware.elf
cp $SRCELF $OUTDIR/elfs/$basename.elf

echo "Generating NRF52 uf2 file"
SRCHEX=.pio/build/$1/firmware.hex
bin/uf2conv.py $SRCHEX -c -o $OUTDIR/bins/$basename.uf2 -f 0xADA52840

echo Generating $ARCHIVEDIR/firmware-$VERSION.zip
rm -f $ARCHIVEDIR/firmware-$VERSION.zip
zip --junk-paths $ARCHIVEDIR/firmware-$VERSION.zip $ARCHIVEDIR/spiffs-$VERSION.bin $OUTDIR/bins/universal/firmware-*-$VERSION.* $OUTDIR/bins/universal/meshtasticd* images/system-info.bin bin/device-install.* bin/device-update.*
echo Generating $ARCHIVEDIR/elfs-$VERSION.zip
rm -f $ARCHIVEDIR/elfs-$VERSION.zip
zip --junk-paths $ARCHIVEDIR/elfs-$VERSION.zip $OUTDIR/elfs/universal/firmware-*-$VERSION.* 
