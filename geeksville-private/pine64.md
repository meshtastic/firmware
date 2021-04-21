# Notes on the pine64 lora board

like before but sx1262 based?

Since both DIO3 and DIO2 not apply to PINE64 LoRa situation, I will wire SX1262 INT [DIO1] pin, contact to CS341F pin 7 INT#  and pin 5.

FIX ch341 GPIO access from linux
RF95 packet RX seems busted FIX FIRST

USE ch341 devboard if needed

SX1262 BUSY seems to come out on pin 15 of the RFM90 HOPE module.  The 'footprint' seems rotated on the pine64 board schematic and that becomes pin 7 on U4 which is "DIO5" on that schematic.  Which goes to pin 8 on the CH341F, which that datasheet calls "IN3"

FIXME - see if possible to read BUSY from "IN3"?

on a ch341a
 *  - Pin 15 (D0/CS0  ) as input/output/CS (CH341_PIN_MODE_IN/CH341_PIN_MODE_OUT/CH341_PIN_MODE_CS) (confirm hooked to CS)
 *  - Pin 16 (D1/CS1  ) as input/output/CS (CH341_PIN_MODE_IN/CH341_PIN_MODE_OUT/CH341_PIN_MODE_CS)
 *  - Pin 17 (D2/CS2  ) as input/output/CS (CH341_PIN_MODE_IN/CH341_PIN_MODE_OUT/CH341_PIN_MODE_CS)
 *  - Pin 19 (D4/DOUT2) as input/output    (CH341_PIN_MODE_IN/CH341_PIN_MODE_OUT) / gpio4 in linux driver / (FIXME: confirm hooked to IRQ also?)
 *  - Pin 21 (D6/DIN2 ) as input           (CH341_PIN_MODE_IN) / called RTS when in UART mode

## ch341-driver

driver busted in 5.11 kernels (rf95 init fails).  5.8.0 is okay, 5.8.18 is okay. fails on 5.10.31, 5.9.16 is okay.  Therefore breakage happened in 5.10 kernels!  Possibly not really breakage, possibly just sloppy caching or something that is more easily caught with modern threading.

cs_change is not being set on the way into the driver!

## gpio

the new GPIO interface https://embeddedbits.org/new-linux-kernel-gpio-user-space-interface/

~/development/meshtastic/meshtastic-esp32$ gpiodetect
gpiochip0 [INT34BB:00] (312 lines)
gpiochip1 [ch341] (2 lines)
~/development/meshtastic/meshtastic-esp32$ gpioinfo 1
gpiochip1 - 2 lines:
	line   0:      "gpio4"       unused   input  active-high 
	line   1:      "gpio5"       unused   input  active-high 
gpiofind gpio4
gpiochip1 0

DO NOT "apt install libgpiod-dev" It doesn't work with kernels newer than about 5.8.  Instead build and install from source: https://git.kernel.org/pub/scm/libs/libgpiod/libgpiod.git/

## Send in patch
Fix drivers/spi/spi.c transfer_once

read about spi: https://elinux.org/images/2/20/Whats_going_on_with_SPI--mark_brown.pdf

https://www.kernel.org/doc/html/v4.17/process/submitting-patches.html 
git clone git://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git