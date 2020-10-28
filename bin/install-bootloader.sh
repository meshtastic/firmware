# You probably don't want to use this script, it programs a custom bootloader build onto a nrf52 board

set -e 

# dependencies
# apt install srecord

BOOTDIR=/home/kevinh/development/meshtastic/Adafruit_nRF52_Bootloader
BOARD=othernet_ppr1
BOOTVER=0.3.2
BOOTNUM=128
BOOTSHA=gc01b9ea
SDCODE=s113
SDVER=7.2.0
PROJ=ppr1

# FIXME for nRF52840 use 0xff000, for nRF52833 use 0x7f000
BOOTSET=0x7f000

nrfjprog --eraseall -f nrf52

# this generates an intel hex file that can be programmed into a NRF52 to tell the adafruit bootloader that the current app image is valid
# Bootloader settings are at BOOTLOADER_SETTINGS (rw) : ORIGIN = 0xFF000, LENGTH = 0x1000
# first 4 bytes should be 0x01 to indicate valid app image
# second 4 bytes should be 0x00 to indicate no CRC required for image
echo "01 00 00 00 00 00 00 00" | xxd -r -p - >/tmp/bootconf.bin
srec_cat /tmp/bootconf.bin -binary -offset $BOOTSET -output /tmp/bootconf.hex -intel   

echo Generating merged hex file from .pio/build/$PROJ/firmware.hex
mergehex -o ${BOARD}_full.hex -m $BOOTDIR/_build/build-$BOARD/${BOARD}_bootloader-$BOOTVER-$BOOTNUM-$BOOTSHA-dirty_${SDCODE}_$SDVER.hex .pio/build/$PROJ/firmware.hex /tmp/bootconf.hex

echo Telling bootloader app region is valid and telling CPU to run
nrfjprog --program ${BOARD}_full.hex -f nrf52 --reset

# nrfjprog --readuicr /tmp/uicr.hex; objdump -s /tmp/uicr.hex | less
