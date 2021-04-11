# RAK Wireless RisBlock

## Docs

https://docs.rakwireless.com/Product-Categories/WisBlock/Quickstart/#wisblock-base-2

GPS module:
Supposedly "Install in slot A only" but I think installing on the back would fit better with the OLED.  FIXME.
https://docs.rakwireless.com/Product-Categories/WisBlock/RAK1910/Overview/#product-description

ST KPS22HB
baro sensor
https://docs.rakwireless.com/Product-Categories/WisBlock/RAK1902/Overview/#product-description

OLED
https://docs.rakwireless.com/Product-Categories/WisBlock/RAK1921/Overview/#product-features
Must be installed on the front for the I2C wires to lineup

Solar enclosure
https://docs.rakwireless.com/Product-Categories/Accessories/RAKBox-B2/Overview/#product-description

Base datasheet (for GPIO mapping)
https://docs.rakwireless.com/Product-Categories/WisBlock/RAK5005-O/Datasheet/#specifications

CPU module carrier (rak4631)
https://docs.rakwireless.com/Product-Categories/WisBlock/RAK4631/Datasheet/#specifications

## TODO

> 3V3_S is another 3.3 V power supply, it can be controlled by the MCU in order to disconnect the power sensors during idle periods to save power. 3V3_S is controlled by IO2 pin on the WisBlock Core board.
Set IO2=1, 3V3_S is on.
Set IO2=0, 3V3_S is off.


* DONE solder header
* DONE attach antenna
* get building (LORA disabled)
* DONE FIX LEDs
* DONE FIX BUTTONs
* DONE FIX I2C assignment
* FIX LORA SPI
* FIX GPS GPIO assignment
* Disable Bluetooth
* Disable LORA
* Boot
* Enable LORA but no TX
* Enable LORA TX
* Enable bluetooth
* Relase as standard part of build (including UF2s)
* Make this doc into a nice HOWTO: what to order, how to connect (which device in which slots), how to install software
* Setup battery voltage sensing
* Set bluetooth PIN support
* Confirm low power draw
* send in PR to https://github.com/geeksville/WisBlock for boards define
* 