#!/usr/bin/env bash

set -e

VERSION=`bin/buildinfo.py long`
SHORT_VERSION=`bin/buildinfo.py short`

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

pio run --environment $1 # -v

cp $BUILDDIR/$basename.elf $OUTDIR/$basename.elf

echo "Copying ESP32 bin file"
cp $BUILDDIR/$basename.factory.bin $OUTDIR/$basename.factory.bin

echo "Copying ESP32 update bin file"
cp $BUILDDIR/$basename.bin $OUTDIR/$basename.bin

echo "Building Filesystem for ESP32 targets"
# If you want to build the webui, uncomment the following lines
# pio run --environment $1 -t buildfs
# cp .pio/build/$1/littlefs.bin $OUTDIR/littlefswebui-$1-$VERSION.bin
# # Remove webserver files from the filesystem and rebuild
# ls -l data/static # Diagnostic list of files
# rm -rf data/static
pio run --environment $1 -t buildfs --disable-auto-clean
cp $BUILDDIR/littlefs-$1-$VERSION.bin $OUTDIR/littlefs-$1-$VERSION.bin
cp bin/device-install.* $OUTDIR/
cp bin/device-update.* $OUTDIR/

# Generate the manifest file
echo "Generating Meshtastic manifest"
TIMEFORMAT="Generated manifest in %E seconds"
time pio run --environment $1 -t mtjson --silent --disable-auto-clean
cp $BUILDDIR/$basename.mt.json $OUTDIR/$basename.mt.json
