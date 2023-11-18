#!/usr/bin/env bash

set -e

VERSION=$(bin/buildinfo.py long)
SHORT_VERSION=$(bin/buildinfo.py short)

OUTDIR=release/

rm -f $OUTDIR/firmware*

mkdir -p $OUTDIR/
rm -r $OUTDIR/* || true

# Important to pull latest version of libs into all device flavors, otherwise some devices might be stale
platformio pkg update

if [[ command -v raspi-config &>/dev/null;  ]] then
	pio run --environment raspbian
	cp .pio/build/raspbian/program $OUTDIR/meshtasticd_linux_arm64
else
	pio run --environment native
	cp .pio/build/native/program $OUTDIR/meshtasticd_linux_amd64
fi

cp bin/device-install.* $OUTDIR
cp bin/device-update.* $OUTDIR
