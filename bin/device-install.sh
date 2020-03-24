#!/bin/bash

set -e

FILENAME=$1

echo "Trying to flash $FILENAME, but first erasing and writing system information"
esptool.py --baud 921600 erase_flash
esptool.py --baud 921600 write_flash 0x1000 system-info.bin
esptool.py --baud 921600 write_flash 0x10000 $FILENAME

