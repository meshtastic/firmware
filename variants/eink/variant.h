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

confirm that watchdog reset (i.e. all pins now become inputs) won't cause the board to power down when we are not connected to USB
(I bet it will). If this happens recommended fix is to add an external pullup on PWR_ON GPIO.

fix bootloader to use two buttons - remove bootloader hacks
fix battery voltage sensing
fix floating point SEGGER printf on nrf52 - see "new NMEA GPS pos"
get second button working in app load
if battery falls too low deassert PWR_ON (to force board to shutdown)
fix display width and height
clean up eink drawing to not have the nasty timeout hack
put eink to sleep when we think the screen is off
enable flash on spi0, share chip selects on spi1.
https://infocenter.nordicsemi.com/index.jsp?topic=%2Fcom.nordic.infocenter.nrf52832.ps.v1.1%2Fspi.html enable reset as a button in
bootloader fix battery pin usage drive TCXO DIO3 enable high whenever we want the clock use PIN_GPS_WAKE to sleep the GPS use
tp_ser_io as a button, it goes high when pressed unify eink display classes
make screen.adjustBrightness() a nop on eink screens
enable gps sleep mode
use new flash chip
add factory/power on self test
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
This serial port is _also_ connected to the incoming D+/D- pins from the USB header.  FIXME, figure out how that is supposed to
work.
*/
#define PIN_SERIAL2_RX (0 + 6)
#define PIN_SERIAL2_TX (0 + 8)
// #define PIN_SERIAL2_EN (0 + 17)

/**
    Wire Interfaces
    */
#define WIRE_INTERFACES_COUNT 1

#define PIN_WIRE_SDA (26) // Not connected on board?
#define PIN_WIRE_SCL (27)

/* touch sensor, active high */

#define TP_SER_IO (0 + 11)

// Board power is enabled either by VBUS from USB or the CPU asserting PWR_ON
#define PIN_PWR_ON (0 + 12)

#define PIN_RTC_INT (0 + 16) // Interrupt from the PCF8563 RTC

/*

FIXME define/FIX flash access

// QSPI Pins
#define PIN_QSPI_SCK 19
#define PIN_QSPI_CS 17
#define PIN_QSPI_IO0 20
#define PIN_QSPI_IO1 21
#define PIN_QSPI_IO2 22
#define PIN_QSPI_IO3 23

// On-board QSPI Flash
#define EXTERNAL_FLASH_DEVICES MX25R6435F
#define EXTERNAL_FLASH_USE_QSPI

*/

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

#define HAS_EINK

#define PIN_SPI1_MISO                                                                                                            \
    (32 + 7) // FIXME not really needed, but for now the SPI code requires something to be defined, pick an used GPIO
#define PIN_SPI1_MOSI PIN_EINK_MOSI
#define PIN_SPI1_SCK PIN_EINK_SCLK

/*
 * Air530 GPS pins
 */

#define PIN_GPS_WAKE (32 + 2)
#define PIN_GPS_PPS (32 + 4)
#define PIN_GPS_TX (32 + 9) // This is for bits going TOWARDS the CPU
#define PIN_GPS_RX (32 + 8) // This is for bits going TOWARDS the GPS

#define PIN_SERIAL1_RX PIN_GPS_TX
#define PIN_SERIAL1_TX PIN_GPS_RX

/*
 * SPI Interfaces
 */
#define SPI_INTERFACES_COUNT 2

// For LORA
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
