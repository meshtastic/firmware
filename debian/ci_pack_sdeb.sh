#!/usr/bin/bash
export DEBEMAIL="jbennett@incomsystems.biz"
export PLATFORMIO_LIBDEPS_DIR=pio/libdeps
export PLATFORMIO_PACKAGES_DIR=pio/packages
export PLATFORMIO_CORE_DIR=pio/core

# Download libraries to `pio`
platformio pkg install -e native
platformio pkg install -e native -t platformio/tool-scons@4.40502.0
# Compress `pio` directory to prevent dh_clean from sanitizing it
tar -cf pio.tar pio/
rm -rf pio
# Download the latest meshtastic/web release build.tar to `web.tar`
curl -L https://github.com/meshtastic/web/releases/download/latest/build.tar -o web.tar

package=$(dpkg-parsechangelog --show-field Source)

rm -rf debian/changelog
dch --create --distribution "$SERIES" --package "$package" --newversion "$PKG_VERSION~$SERIES" \
	"GitHub Actions Automatic packaging for $PKG_VERSION~$SERIES"

# Build the source deb
debuild -S -nc -k"$GPG_KEY_ID"
