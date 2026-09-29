#!/usr/bin/env bash

set -e

VERSION=$(bin/buildinfo.py long)

BUILDDIR=.pio/build/$1
OUTDIR=release

rm -f $OUTDIR/firmware*
rm -r $OUTDIR/* || true

# Important to pull latest version of libs into all device flavors, otherwise some devices might be stale
platformio pkg install -e $1

echo "Building for $1 with $PLATFORMIO_BUILD_FLAGS"
rm -f $BUILDDIR/firmware*

# The shell vars the build tool expects to find
export APP_VERSION=$VERSION

basename=firmware-$1-$VERSION
ota_basename=${basename}-ota

pio run --environment $1 -t dfu -t mtjson # -v

cp $BUILDDIR/$basename.elf $OUTDIR/$basename.elf

echo "Copying merged hex (bootloader + SoftDevice + application)"
cp $BUILDDIR/$basename.hex $OUTDIR/$basename.hex

echo "Copying nRF54L dfu (OTA) file"
cp $BUILDDIR/$basename.zip $OUTDIR/$ota_basename.zip

echo "Copying manifest"
cp $BUILDDIR/$basename.mt.json $OUTDIR/$basename.mt.json || true
