# RAK Wireless RisBlock

## Docs

https://docs.rakwireless.com/Product-Categories/WisBlock/Quickstart/#wisblock-base-2

FIXME - list required, recommended and optional components

GPS module:
Supposedly "Install in slot A only" but I think installing on the back would fit better with the OLED.  FIXME.
https://docs.rakwireless.com/Product-Categories/WisBlock/RAK1910/Overview/#product-description

ST LPS22HB 
baro & temp sensor, i2c address 0x5c
https://docs.rakwireless.com/Product-Categories/WisBlock/RAK1902/Overview/#product-description
https://www.st.com/en/mems-and-sensors/lps22hb.html
https://www.st.com/resource/en/datasheet/lps22hb.pdf

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

* Turn off external 3V3 supply when not using GPS to save power!
> 3V3_S is another 3.3 V power supply, it can be controlled by the MCU in order to disconnect the power sensors during idle periods to save power. 3V3_S is controlled by IO2 pin on the WisBlock Core board.
Set IO2=1, 3V3_S is on.
Set IO2=0, 3V3_S is off.

* Enable bluetooth
* Relase as standard part of build (including UF2s)
* Make this doc into a nice HOWTO: what to order, how to connect (which device in which slots), how to install software
* Setup battery voltage sensing
* Set bluetooth PIN support
* Confirm low power draw
* Confirm that OLED works
* send in PR to https://github.com/geeksville/WisBlock for boards define
* 