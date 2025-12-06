#!/usr/bin/env bash

set -e
pio run --environment native
gdbserver --once localhost:2345 .pio/build/native/program "$@"
