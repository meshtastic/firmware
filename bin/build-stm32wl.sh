#!/usr/bin/env bash

set -e

VERSION=$(bin/buildinfo.py long)
SHORT_VERSION=$(bin/buildinfo.py short)

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

pio run --environment $1 -t mtjson # -v

cp $BUILDDIR/$basename.elf $OUTDIR/$basename.elf

echo "Copying STM32 bin file"
cp $BUILDDIR/$basename.bin $OUTDIR/$basename.bin

echo "Copying manifest"
cp $BUILDDIR/$basename.mt.json $OUTDIR/$basename.mt.json
