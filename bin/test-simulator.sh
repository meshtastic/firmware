#!/usr/bin/env bash

set -e

echo "Starting simulator"
.pio/build/native/program &
sleep 20 # 5 seconds was not enough

echo "Simulator started, launching python test..."
python3 -c 'from meshtastic.test import testSimulator; testSimulator()'

