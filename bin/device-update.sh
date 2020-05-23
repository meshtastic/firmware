#!/bin/bash

set -e

FILENAME=$1

echo "Trying to update $FILENAME"
esptool.py --baud 921600 write_flash 0x10000 $FILENAME
