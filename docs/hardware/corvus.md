# Notes on @BigCorvus boards

## Board version 1.1

variant name lora_relay_v1

### Remaining TODOs

- power hold for the ST7735
- look at example sketch
- turn on xmit boost

## Recommendations for future boards

@BigCorvus your board is **really** nice. Here's some ideas for the future:

- make the SWDIO header more standard (the small ARM 2x5 micro footprint?) or at least througholes so it is easy to solder a header

## How to program bootloader

Download from here: https://github.com/adafruit/Adafruit_nRF52_Bootloader/releases

```
nrfjprog -f nrf52 --eraseall
Erasing user available code and UICR flash areas.
Applying system reset.

nrfjprog -f nrf52 --program feather_nrf52840_express_bootloader-0.3.2_s140_6.1.1.hex
Parsing hex file.
Reading flash area to program to guarantee it is erased.
Checking that the area to write is not protected.
Programming device.
```

Then reboot the board, if all went well it now shows up as a mountable filesystem on your USB bus.
