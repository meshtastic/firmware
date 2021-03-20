esptool.py --baud 921600 write_flash 0x10000 release/archive/old/firmware-tbeam-EU865-1.0.0.bin
echo "Erasing the otadata partition, which will turn off flash flippy-flop and force the first image to be used"
esptool.py --baud 921600 erase_region 0xe000 0x2000
