#!/usr/bin/bash
export PLATFORMIO_LIBDEPS_DIR=pio/libdeps
export PLATFORMIO_PACKAGES_DIR=pio/packages

# Download libraries to `libdeps`
platformio pkg install -e native

# Build the source deb
debuild -S -k$GPG_KEY_ID
