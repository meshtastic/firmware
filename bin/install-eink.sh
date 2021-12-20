# You probably don't want to use this script, it programs a custom bootloader build onto a nrf52 board

set -e 

BOOTDIR=/home/kevinh/development/meshtastic/Adafruit_nRF52_Bootloader

nrfjprog --eraseall -f nrf52

# to get tool run "sudo apt-get install srecord"

# this generates an intel hex file that can be programmed into a NRF52 to tell the adafruit bootloader that the current app image is valid
# Bootloader settings are at BOOTLOADER_SETTINGS (rw) : ORIGIN = 0xFF000, LENGTH = 0x1000
# first 4 bytes should be 0x01 to indicate valid app image
# second 4 bytes should be 0x00 to indicate no CRC required for image
echo "01 00 00 00 00 00 00 00" | xxd -r -p - >/tmp/bootconf.bin
srec_cat /tmp/bootconf.bin -binary -offset 0xff000 -output /tmp/bootconf.hex -intel   

echo Generating merged hex file 
mergehex -m $BOOTDIR/_build/build-ttgo_eink/ttgo_eink_bootloader-0.3.2-213-gf67f592-dirty_s140_6.1.1.hex .pio/build/eink/firmware.hex /tmp/bootconf.hex -o ttgo_eink_full.hex

echo Telling bootloader app region is valid and telling CPU to run
nrfjprog --program ttgo_eink_full.hex -f nrf52 --reset

# nrfjprog --readuicr /tmp/uicr.hex; objdump -s /tmp/uicr.hex | less
