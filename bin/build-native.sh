#!/usr/bin/env bash

set -e

platformioFailed() {
	[[ $VIRTUAL_ENV != "" ]] && exit 1 # don't hint at virtualenv if it's already in use
	echo -e "\nThere were issues running platformio and you are not using a virtual environment." \
		"\nYou may try setting up virtualenv and downloading the latest platformio from pip:" \
		"\n\tvirtualenv venv" \
		"\n\tsource venv/bin/activate" \
		"\n\tpip install platformio" \
		"\n\t./bin/build-native.sh # retry building"
	exit 1
}

VERSION=$(bin/buildinfo.py long)
SHORT_VERSION=$(bin/buildinfo.py short)

OUTDIR=release/

rm -f $OUTDIR/firmware*

mkdir -p $OUTDIR/
rm -r $OUTDIR/* || true

# Important to pull latest version of libs into all device flavors, otherwise some devices might be stale
platformio pkg update --environment native || platformioFailed
pio run --environment native || platformioFailed
cp .pio/build/native/program "$OUTDIR/meshtasticd_linux_$(uname -m)"
cp bin/native-install.* $OUTDIR
