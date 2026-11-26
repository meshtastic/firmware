#!/usr/bin/env bash

set -e

VERSION=`bin/buildinfo.py long`
SHORT_VERSION=`bin/buildinfo.py short`

BUILDDIR=.pio/build/$1
OUTDIR=release

rm -f $OUTDIR/firmware*
rm -r $OUTDIR/* || true

# Important to pull latest version of libs into all device flavors, otherwise some devices might be stale
# platformio pkg install -e $1
# ...redundant with pioarduino

pio --version

echo "Building for $1 with $PLATFORMIO_BUILD_FLAGS"
rm -f $BUILDDIR/firmware*

# The shell vars the build tool expects to find
export APP_VERSION=$VERSION

basename=firmware-$1-$VERSION

# pioarduino's IDF-libs pass resolves espressif/fb_gfx at esp32-arduino-lib-builder's master, so
# ESP-IDF's component manager runs `git fetch` on that repo in every build, and a reset connection
# fails the build at CMake configure. Retry the build for that error only; anything else fails at once.
ATTEMPTS=${ESP32_IDF_FETCH_ATTEMPTS:-3}
if [[ ! $ATTEMPTS =~ ^[1-9][0-9]*$ ]]; then
	echo "ESP32_IDF_FETCH_ATTEMPTS must be a positive integer, got '$ATTEMPTS'" >&2
	exit 2
fi

buildlog=$(mktemp)
trap 'rm -f "$buildlog"' EXIT
for attempt in $(seq 1 "$ATTEMPTS"); do
	set +e
	pio run --environment "$1" -t mtjson 2>&1 | tee "$buildlog" # -v
	status=${PIPESTATUS[0]}
	set -e
	if [[ $status -eq 0 ]]; then
		break
	fi
	# CMake wraps the error across lines, so match it with the whitespace folded.
	if [[ $attempt -ge $ATTEMPTS ]] ||
		! tr -s '[:space:]' ' ' <"$buildlog" | grep -qE "ComponentManager/b_git_[0-9a-f]+ fetch origin' failed"; then
		exit "$status"
	fi
	delay=$((attempt * 15))
	echo "ESP-IDF component fetch failed on attempt $attempt of $ATTEMPTS for $1; retrying in ${delay}s" >&2
	sleep "$delay"
done

cp $BUILDDIR/$basename.elf $OUTDIR/$basename.elf

echo "Copying linker scripts and map file"
cp $BUILDDIR/$basename.*.ld $OUTDIR/ 2>/dev/null || true
cp $BUILDDIR/$basename.map $OUTDIR/ 2>/dev/null || true

echo "Copying ESP32 bin file"
cp $BUILDDIR/$basename.factory.bin $OUTDIR/$basename.factory.bin

echo "Copying ESP32 update bin file"
cp $BUILDDIR/$basename.bin $OUTDIR/$basename.bin

echo "Copying Filesystem for ESP32 targets"
cp $BUILDDIR/littlefs-$1-$VERSION.bin $OUTDIR/littlefs-$1-$VERSION.bin
cp bin/device-install.* $OUTDIR/
cp bin/device-update.* $OUTDIR/

echo "Copying manifest"
cp $BUILDDIR/$basename.mt.json $OUTDIR/$basename.mt.json
