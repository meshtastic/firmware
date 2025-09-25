#!/usr/bin/bash
export DEBFULLNAME="GitHub Actions"
export DEBEMAIL="github-actions[bot]@users.noreply.github.com"
PKG_VERSION=$(python3 bin/buildinfo.py short)

dch --newversion "$PKG_VERSION.0" \
	--distribution unstable \
	"Version $PKG_VERSION"
