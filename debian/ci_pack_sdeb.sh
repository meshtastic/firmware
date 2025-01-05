#!/usr/bin/bash
export DEBEMAIL="github-actions[bot]@users.noreply.github.com"
export PLATFORMIO_LIBDEPS_DIR=pio/libdeps
export PLATFORMIO_PACKAGES_DIR=pio/packages

# Download libraries to `libdeps`
platformio pkg install -e native

package=$(dpkg-parsechangelog --show-field Source)
pkg_version=$(dpkg-parsechangelog --show-field Version | cut -d- -f1)

dch --create --distribution $SERIES --package $package --newversion $pkg_version-ppa${REVISION::7}~$SERIES \
	"GitHub Actions Automatic packaging for $SERIES"

# Build the source deb
debuild -S -k$GPG_KEY_ID
