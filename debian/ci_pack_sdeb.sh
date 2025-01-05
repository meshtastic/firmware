#!/usr/bin/bash
export DEBEMAIL="jbennett@incomsystems.biz"
export PLATFORMIO_LIBDEPS_DIR=pio/libdeps
export PLATFORMIO_PACKAGES_DIR=pio/packages
export PLATFORMIO_CORE_DIR=.pio_core

# Download libraries to `libdeps`
platformio pkg install -e native

package=$(dpkg-parsechangelog --show-field Source)

rm -rf debian/changelog
dch --create --distribution $SERIES --package $package --newversion $PKG_VERSION~$SERIES \
	"GitHub Actions Automatic packaging for $PKG_VERSION~$SERIES"

# Build the source deb
debuild -S -k$GPG_KEY_ID
