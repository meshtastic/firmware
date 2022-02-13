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

pio run --environment native
cp .pio/build/native/program $OUTDIR/bins/universal/meshtasticd_linux_amd64

cp bin/device-install.* $OUTDIR
cp bin/device-update.* $OUTDIR

