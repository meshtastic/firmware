#!/bin/bash

set -e

FILENAME=$1

echo "Trying to update $FILENAME"
esptool.py --baud 921600 writeflash 0x10000 $FILENAME
