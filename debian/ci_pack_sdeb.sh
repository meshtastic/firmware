#!/usr/bin/bash
export DEBEMAIL="jbennett@incomsystems.biz"
export PLATFORMIO_LIBDEPS_DIR=pio/libdeps
export PLATFORMIO_PACKAGES_DIR=pio/packages
export PLATFORMIO_CORE_DIR=pio/core

# Download libraries to `libdeps`
platformio pkg install -e native
platformio pkg install -t platformio/tool-scons@4.40801.0
tar -cf pio.tar pio/
rm -rf pio

package=$(dpkg-parsechangelog --show-field Source)

rm -rf debian/changelog
dch --create --distribution $SERIES --package $package --newversion $PKG_VERSION~$SERIES \
	"GitHub Actions Automatic packaging for $PKG_VERSION~$SERIES"

# Build the source deb
debuild -S -nc -k$GPG_KEY_ID
