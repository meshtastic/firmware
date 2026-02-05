#!/usr/bin/env sh

git submodule update --init

pip install --no-cache-dir setuptools
pipx install esptool
