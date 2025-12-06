#!/usr/bin/env bash

set -e 

echo "This script is only for developers who are publishing new builds on github.  Most users don't need it"

VERSION=`bin/buildinfo.py long`

# Must have a V prefix to trigger github
git tag "v${VERSION}"

git push origin "v${VERSION}" # push the tag

echo "Tag ${VERSION} pushed to github, github actions should now be building the draft release.  If it seems good, click to publish it"
