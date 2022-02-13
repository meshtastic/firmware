#!/usr/bin/env bash

set -e

VERSION=`bin/buildinfo.py long`
SHORT_VERSION=`bin/buildinfo.py short`

OUTDIR=release/latest

rm -f $OUTDIR/firmware*

mkdir -p $OUTDIR/bins
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

echo "Copying ESP32 bin file"
SRCBIN=.pio/build/$1/firmware.bin
cp $SRCBIN $OUTDIR/bins/$basename.bin

echo "Building SPIFFS for ESP32 targets"
pio run --environment tbeam -t buildfs
cp .pio/build/tbeam/spiffs.bin $OUTDIR/bins/universal/spiffs-$VERSION.bin