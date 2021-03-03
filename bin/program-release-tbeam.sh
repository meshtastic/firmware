
set -e

VERSION=`bin/buildinfo.py`
FILENAME=release/latest/bins/universal/firmware-tbeam-$VERSION.bin

echo Installing $FILENAME
esptool.py --baud 921600 write_flash 0x10000 $FILENAME
