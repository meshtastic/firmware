#!/usr/bin/env bash

set -e
pio run --environment native
.pio/build/native/program "$@"
