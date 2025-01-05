#!/usr/bin/bash
export DEBEMAIL="github-actions[bot]@users.noreply.github.com"

dch --newversion "$(python3 bin/buildinfo.py short)-1" \
        --distribution unstable \
        "GitHub Actions Automatic version bump"
