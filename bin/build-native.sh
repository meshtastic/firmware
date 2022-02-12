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

pio run --environment native
cp .pio/build/native/program $OUTDIR/bins/universal/meshtasticd_linux_amd64

echo Generating $ARCHIVEDIR/firmware-$VERSION.zip
rm -f $ARCHIVEDIR/firmware-$VERSION.zip
zip --junk-paths $ARCHIVEDIR/firmware-$VERSION.zip $ARCHIVEDIR/spiffs-$VERSION.bin $OUTDIR/bins/universal/firmware-*-$VERSION.* $OUTDIR/bins/universal/meshtasticd* images/system-info.bin bin/device-install.* bin/device-update.*
echo Generating $ARCHIVEDIR/elfs-$VERSION.zip
rm -f $ARCHIVEDIR/elfs-$VERSION.zip
zip --junk-paths $ARCHIVEDIR/elfs-$VERSION.zip $OUTDIR/elfs/universal/firmware-*-$VERSION.* 

