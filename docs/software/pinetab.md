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

## Task list

* Port meshtastic to build (under platformio) for a poxix target.  spec: no screen, no gpios, sim network interface, posix threads, posix semaphores & queues, IO to the console only
Use ARM linux: https://platformio.org/platforms/linux_arm
And  linux native: https://platformio.org/platforms/native

* Test cs341 driver - just test reading/writing a register and detecting interrupts, confirm can see rf95
* Make a radiolib spi module that targets the cs341 (and builds on linux)
* use new radiolib module to hook pinebook lora to meshtastic, confirm mesh discovery works
* Make a subclass of StreamAPI that works as a posix TCP server
* Use new TCP endpoint from meshtastic-python
