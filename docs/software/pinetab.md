# Pinetab

These are **preliminary** notes on support for Meshtastic in the Pinetab.

A RF95 is connected via a CS341 USB-SPI chip.

Pin assignments:
CS0 from RF95 goes to CS0 on CS341
DIO0 from RF95 goes to INT on CS341
RST from RF95 goes to RST on CS341

This linux driver claims to provide USB-SPI support: https://github.com/gschorcht/spi-ch341-usb
Notes here on using that driver: https://www.linuxquestions.org/questions/linux-hardware-18/ch341-usb-to-spi-adaptor-driver-doesn%27t-work-4175622736/

Or if **absolutely** necessary could bitbang: https://www.cnx-software.com/2018/02/16/wch-ch341-usb-to-serial-chip-gets-linux-driver-to-control-gpios-over-usb/
