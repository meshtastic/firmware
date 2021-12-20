set -e

echo "building for t-echo"
pio run --environment t-echo

echo "Converting to uf2 for NRF52 Adafruit bootloader - double tap on the reset button to force bootloader entry"
bin/uf2conv.py .pio/build/t-echo/firmware.hex -f 0xADA52840
cp flash.uf2 /media/kevinh/FTH*BOOT/
