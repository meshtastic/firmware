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
ota_basename=${basename}-ota

pio run --environment $1 -t mtjson # -v

cp $BUILDDIR/$basename.elf $OUTDIR/$basename.elf

echo "Copying NRF52 dfu (OTA) file"
cp $BUILDDIR/$basename.zip $OUTDIR/$ota_basename.zip

echo "Copying NRF52 UF2 file"
cp $BUILDDIR/$basename.uf2 $OUTDIR/$basename.uf2
cp bin/*.uf2 $OUTDIR/

SRCHEX=$BUILDDIR/$basename.hex

# if WM1110 target, copy the merged.hex
if (echo $1 | grep -q "wio-sdk-wm1110"); then
	echo "Copying .merged.hex file"
	SRCHEX=$BUILDDIR/$basename.merged.hex
	cp $SRCHEX $OUTDIR/
fi

if (echo $1 | grep -q "rak4631"); then
	echo "Copying .hex file"
	cp $SRCHEX $OUTDIR/
fi

echo "Copying manifest"
cp $BUILDDIR/$basename.mt.json $OUTDIR/$basename.mt.json
