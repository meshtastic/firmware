# Meshtastic Branding / Whitelabeling

This directory is consumed during the creation of **event** firmware.

`bin/platformio-custom.py` determines the display resolution, and locates the corresponding `logo_<width>x<height>.png`.

Ex:

- `logo_800x480.png`
- `logo_480x480.png`
- `logo_480x320.png`
- `logo_320x480.png`
- `logo_320x240.png`

This file is copied to `data/boot/logo.png` before filesytem image compilation.

For additional examples see the [`event/defcon33` branch](https://github.com/meshtastic/firmware/tree/event/defcon33).
