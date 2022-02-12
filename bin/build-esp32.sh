#!/usr/bin/env bash

set -e

VERSION=`bin/buildinfo.py long`
SHORT_VERSION=`bin/buildinfo.py short`

OUTDIR=release/latest

# We keep all old builds (and their map files in the archive dir)
ARCHIVEDIR=release/archive 

rm -f $OUTDIR/firmware*

mkdir -p $OUTDIR/bins $ARCHIVEDIR
rm -r $OUTDIR/bins/* || true
mkdir -p $OUTDIR/bins/universal $OUTDIR/elfs/universal

# Make sure our submodules are current
git submodule update 

# Important to pull latest version of libs into all device flavors, otherwise some devices might be stale
platformio lib update 

BOARD=$1

echo "Building for $BOARD with $PLATFORMIO_BUILD_FLAGS"
rm -f .pio/build/$BOARD/firmware.*

# The shell vars the build tool expects to find
export APP_VERSION=$VERSION

# Are we building a universal/regionless rom?
export HW_VERSION="1.0"
basename=universal/firmware-$BOARD-$VERSION

pio run --environment $BOARD # -v
SRCELF=.pio/build/$BOARD/firmware.elf
cp $SRCELF $OUTDIR/elfs/$basename.elf

echo "Copying ESP32 bin file"
SRCBIN=.pio/build/$BOARD/firmware.bin
cp $SRCBIN $OUTDIR/bins/$basename.bin

echo "Building SPIFFS for ESP32 targets"
pio run --environment tbeam -t buildfs
cp .pio/build/tbeam/spiffs.bin $OUTDIR/bins/universal/spiffs-$VERSION.bin

# keep the bins in archive also
cp $OUTDIR/bins/universal/spiffs* $OUTDIR/bins/universal/firmware* $OUTDIR/elfs/universal/firmware* $ARCHIVEDIR

echo Generating $ARCHIVEDIR/firmware-$VERSION.zip
rm -f $ARCHIVEDIR/firmware-$VERSION.zip
zip --junk-paths $ARCHIVEDIR/firmware-$VERSION.zip $ARCHIVEDIR/spiffs-$VERSION.bin $OUTDIR/bins/universal/firmware-*-$VERSION.* $OUTDIR/bins/universal/meshtasticd* images/system-info.bin bin/device-install.* bin/device-update.*
echo Generating $ARCHIVEDIR/elfs-$VERSION.zip
rm -f $ARCHIVEDIR/elfs-$VERSION.zip
zip --junk-paths $ARCHIVEDIR/elfs-$VERSION.zip $OUTDIR/elfs/universal/firmware-*-$VERSION.* 

echo BUILT ALL
