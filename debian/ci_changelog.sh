#!/usr/bin/bash
export DEBEMAIL="github-actions[bot]@users.noreply.github.com"
PKG_VERSION=$(python3 bin/buildinfo.py short)

dch --newversion "$PKG_VERSION-1" \
	--distribution UNRELEASED \
	"GitHub Actions Automatic version bump"
