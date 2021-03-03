
set -e

VERSION=`bin/buildinfo.py`

echo Installing release/latest/bins/firmware-tbeam-US-$VERSION.bin
esptool.py --baud 921600 write_flash 0x10000 release/latest/bins/firmware-tbeam-US-$VERSION.bin
