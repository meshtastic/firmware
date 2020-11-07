/*
 Copyright (c) 2014-2015 Arduino LLC.  All right reserved.
 Copyright (c) 2016 Sandeep Mistry All right reserved.
 Copyright (c) 2018, Adafruit Industries (adafruit.com)

 This library is free software; you can redistribute it and/or
 modify it under the terms of the GNU Lesser General Public
 License as published by the Free Software Foundation; either
 version 2.1 of the License, or (at your option) any later version.
 This library is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 See the GNU Lesser General Public License for more details.
 You should have received a copy of the GNU Lesser General Public
 License along with this library; if not, write to the Free Software
 Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

#ifndef _VARIANT_TTGO_EINK_V1_
#define _VARIANT_TTGO_EINK_V1_

/** Master clock frequency */
#define VARIANT_MCK (64000000ul)

#define USE_LFXO // Board uses 32khz crystal for LF

/*
@geeksville eink TODO:

soonish:
DONE hook cdc acm device to debug output
DONE fix bootloader to use two buttons - remove bootloader hacks
DONE get second button working in app load
DONE use tp_ser_io as a button, it goes high when pressed unify eink display classes
fix display width and height
clean up eink drawing to not have the nasty timeout hack
measure current draws
DONE put eink to sleep when we think the screen is off
enable gps sleep mode
turn off txco on lora?
make screen.adjustBrightness() a nop on eink screens

later:
enable flash on qspi.
fix floating point SEGGER printf on nrf52 - see "new NMEA GPS pos"
add factory/power on self test

feedback to give:

* bootloader is finished

* the capacitive touch sensor works, though I'm not sure what use you are intending for it

* remove ipx connector for nfc, instead use two caps and loop traces on the back of the board as an antenna?

* the i2c RTC seems to talk fine on the i2c bus.  However, I'm not sure of the utility of that part.  Instead I'd be in favor of
the following:

* move BAT1 to power the GPS VBACKUP instead per page 6 of the Air530 datasheet.  And remove the i2c RTC entirely.

* remove the cp2014 chip.

* I've made the serial flash chip work, but if you do a new spin of the board I recommend:
connect pin 3 and pin 7 of U4 to spare GPIOs on the processor (instead of their current connections),
This would allow using 4 bit wide interface mode to the serial flash - doubling the transfer speed! see example here:
https://infocenter.nordicsemi.com/topic/ug_nrf52840_dk/UG/nrf52840_DK/hw_external_memory.html?cp=4_0_4_7_4
Once again - I'm glad you added that external flash chip.

* Power measurements
When powered by 4V battery

CPU on, lora radio RX mode, bluetooth enabled, GPS trying to lock.  total draw 43mA
CPU on, lora radio RX mode, bluetooth enabled, GPS super low power sleep mode.  Total draw 20mA
CPU on, lora radio TX mode, bluetooth enabled, GPS super low power sleep mode.  Total draw 132mA

Note: power consumption while connected via BLE to a phone almost identical.

Note: eink display for all tests was in sleep mode most of the time.  Current draw during the brief periods while the eink was being drawn was not
measured (but it was low).

Note: Turning off EINK PWR_ON produces no noticeable power savings over just putting the eink display into sleep mode.

*/

/*----------------------------------------------------------------------------
 *        Headers
 *----------------------------------------------------------------------------*/

#include "WVariant.h"

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

// Number of pins defined in PinDescription array
#define PINS_COUNT (48)
#define NUM_DIGITAL_PINS (48)
#define NUM_ANALOG_INPUTS (1)
#define NUM_ANALOG_OUTPUTS (0)

// LEDs
#define PIN_LED1 (0 + 13) // green (but red on my prototype)
#define PIN_LED2 (0 + 15) // blue (but red on my prototype)
#define PIN_LED3 (0 + 14) // red (not functional on my prototype)

#define LED_RED PIN_LED3
#define LED_GREEN PIN_LED1
#define LED_BLUE PIN_LED2

#define LED_BUILTIN LED_GREEN
#define LED_CONN PIN_BLUE

#define LED_STATE_ON 0 // State when LED is lit
#define LED_INVERTED 1

/*
 * Buttons
 */
#define PIN_BUTTON1 (32 + 10)
#define PIN_BUTTON2 (0 + 18) // 0.18 is labeled on the board as RESET but we configure it in the bootloader as a regular GPIO

/*
 * Analog pins
 */
#define PIN_A0 (4) // Battery ADC

#define BATTERY_PIN PIN_A0

static const uint8_t A0 = PIN_A0;

#define ADC_RESOLUTION 14

#define PIN_NFC1 (9)
#define PIN_NFC2 (10)

/*
 * Serial interfaces
 */

/*
No longer populated on PCB
*/
//#define PIN_SERIAL2_RX (0 + 6)
//#define PIN_SERIAL2_TX (0 + 8)
// #define PIN_SERIAL2_EN (0 + 17)

/**
    Wire Interfaces
    */
#define WIRE_INTERFACES_COUNT 1

#define PIN_WIRE_SDA (26) // Not connected on board?
#define PIN_WIRE_SCL (27)

/* touch sensor, active high */

#define TP_SER_IO (0 + 11)

#define PIN_RTC_INT (0 + 16) // Interrupt from the PCF8563 RTC

/*
External serial flash WP25R1635FZUIL0
*/

// QSPI Pins
#define PIN_QSPI_SCK (32 + 14)
#define PIN_QSPI_CS (32 + 15)
#define PIN_QSPI_IO0 (32 + 12) // MOSI if using two bit interface
#define PIN_QSPI_IO1 (32 + 13) // MISO if using two bit interface
//#define PIN_QSPI_IO2 22 // WP if using two bit interface (i.e. not used)
//#define PIN_QSPI_IO3 23 // HOLD if using two bit interface (i.e. not used)

// On-board QSPI Flash
#define EXTERNAL_FLASH_DEVICES MX25R1635F
#define EXTERNAL_FLASH_USE_QSPI

/*
 * Lora radio
 */

#define SX1262_CS (0 + 24) // FIXME - we really should define LORA_CS instead
#define SX1262_DIO1 (0 + 20)
// Note DIO2 is attached internally to the module to an analog switch for TX/RX switching
#define SX1262_DIO3                                                                                                              \
    (0 + 21) // This is used as an *output* from the sx1262 and connected internally to power the tcxo, do not drive from the main
             // CPU?
#define SX1262_BUSY (0 + 17)
#define SX1262_RESET (0 + 25)
#define SX1262_E22 // Not really an E22 but TTGO seems to be trying to clone that
// Internally the TTGO module hooks the SX1262-DIO2 in to control the TX/RX switch (which is the default for the sx1262interface
// code)

// #define LORA_DISABLE_SENDING // Define this to disable transmission for testing (power testing etc...)

/*
 * eink display pins
 */

#define PIN_EINK_EN (32 + 11)
#define PIN_EINK_CS (0 + 30)
#define PIN_EINK_BUSY (0 + 3)
#define PIN_EINK_DC (0 + 28)
#define PIN_EINK_RES (0 + 2)
#define PIN_EINK_SCLK (0 + 31)
#define PIN_EINK_MOSI (0 + 29) // also called SDI

// Controls power for the eink display - Board power is enabled either by VBUS from USB or the CPU asserting PWR_ON
// FIXME - I think this is actually just the board power enable - it enables power to the CPU also 
#define PIN_EINK_PWR_ON (0 + 12)

#define HAS_EINK

// No screen wipes on eink
#define SCREEN_TRANSITION_MSECS 0

#define PIN_SPI1_MISO                                                                                                            \
    (32 + 7) // FIXME not really needed, but for now the SPI code requires something to be defined, pick an used GPIO
#define PIN_SPI1_MOSI PIN_EINK_MOSI
#define PIN_SPI1_SCK PIN_EINK_SCLK

/*
 * Air530 GPS pins
 */

#define PIN_GPS_WAKE (32 + 2) // An output to wake GPS, low means allow sleep, high means force wake
#define PIN_GPS_PPS (32 + 4)  // Pulse per second input from the GPS
#define PIN_GPS_TX (32 + 9)   // This is for bits going TOWARDS the CPU
#define PIN_GPS_RX (32 + 8)   // This is for bits going TOWARDS the GPS

#define HAS_AIR530_GPS

#define PIN_SERIAL1_RX PIN_GPS_TX
#define PIN_SERIAL1_TX PIN_GPS_RX

/*
 * SPI Interfaces
 */
#define SPI_INTERFACES_COUNT 2

// For LORA, spi 0
#define PIN_SPI_MISO (0 + 23)
#define PIN_SPI_MOSI (0 + 22)
#define PIN_SPI_SCK (0 + 19)

// To debug via the segger JLINK console rather than the CDC-ACM serial device
#define USE_SEGGER

#ifdef __cplusplus
}
#endif

/*----------------------------------------------------------------------------
 *        Arduino objects - C++ only
 *----------------------------------------------------------------------------*/

#endif
