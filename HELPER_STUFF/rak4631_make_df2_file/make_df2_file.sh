unzip ../../.pio/build/rak4631/firmware.zip -d ../../.pio/build/rak4631/firmware
python3 uf2conv.py ../../.pio/build/rak4631/firmware/firmware.bin -c -b 0x26000 -f 0xADA52840
rm -rf ../../.pio/build/rak4631/firmware
