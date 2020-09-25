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

#ifndef _VARIANT_LORA_RELAY_V1_
#define _VARIANT_LORA_RELAY_V1_

/** Master clock frequency */
#define VARIANT_MCK (64000000ul)

#define USE_LFXO // Board uses 32khz crystal for LF
// define USE_LFRC    // Board uses RC for LF

/*
@geeksville eink TODO:
fix battery pin usage
drive TCXO DIO3 enable high whenever we want the clock
use PIN_GPS_WAKE to sleep the GPS
use tp_ser_io as a button, it goes high when pressed

eink probably is // #include <GxGDEP015OC1/GxGDEP015OC1.h>    // 1.54" b/w               //G702-A
https://github.com/Xinyuan-LilyGO/LilyGO_T5_V24
200 x 200

feedback to ttgo:
name: TTGO LoraCard (nice googablity, unique name, sounds slick, implies lora and small)
i'm going to add some sort of pass/fail factory test
remove the cp2014 part
add solar power
move touch sensor pad to the same side of the PCB as the screen (or just change it to a button)
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
#define PIN_LED1 (0 + 13)
#define PIN_LED2 (0 + 14)
#define PIN_LED3 (0 + 15)

#define LED_RED PIN_LED2
#define LED_GREEN PIN_LED1
#define LED_BLUE PIN_LED3

#define LED_BUILTIN LED_GREEN
#define LED_CONN LED_CONN

#define LED_STATE_ON 0 // State when LED is lit

/*
 * Buttons
 */
#define PIN_BUTTON1 (32 + 3)

/*
 * Analog pins
 */
#define PIN_A0 (4) // Battery ADC

// #define BATTERY_PIN PIN_A0

static const uint8_t A0 = PIN_A0;

#define ADC_RESOLUTION 14

// Other pins
/*
#define PIN_AREF (2)
#define PIN_NFC1 (9)
#define PIN_NFC2 (10)

static const uint8_t AREF = PIN_AREF;
*/

/*
 * Serial interfaces
 */

/*
This serial port is _also_ connected to the incoming D+/D- pins from the USB header.  FIXME, figure out how that is supposed to
work.
*/
#define PIN_SERIAL2_RX (32 + 9)
#define PIN_SERIAL2_TX (32 + 8)
// #define PIN_SERIAL2_EN (0 + 17)

// Connected to Jlink CDC
// #define PIN_SERIAL2_RX (8)
// #define PIN_SERIAL2_TX (6)

/*
 * Wire Interfaces
 */
#define WIRE_INTERFACES_COUNT 1

#define PIN_WIRE_SDA (26) // Not connected on board?
#define PIN_WIRE_SCL (27)

/* touch sensor, active high */

#define TP_SER_IO (32 + 1)

// Board power is enabled either by VBUS from USB or the CPU asserting PWR_ON
#define PIN_PWR_ON (32 + 2)

/*
 * Lora radio
 */

#define SX1262_CS (0 + 24) // FIXME - we really should define LORA_CS instead
#define SX1262_DIO1 (0 + 20)
// Note DIO2 is attached internally to the module to an analog switch for TX/RX switching
#define SX1262_DIO3                                                                                                              \
    (0 + 22) // This is used as an *output* from the sx1262 and connected internally to power the tcxo, do not drive from the main
             // CPU?
#define SX1262_BUSY (0 + 25)
#define SX1262_RESET (32 + 0)
#define SX1262_E22 // Not really an E22 but TTGO seems to be trying to clone that
// Internally the TTGO module hooks the SX1262-DIO2 in to control the TX/RX switch (which is the default for the sx1262interface
// code)

#define LORA_DISABLE_SENDING // Define this to disable transmission for testing (power testing etc...)

/*
 * eink display pins
 */

#define PIN_EINK_EN (0 + 11)
#define PIN_EINK_CS (0 + 30)
#define PIN_EINK_BUSY (0 + 3)
#define PIN_EINK_DC (0 + 28)
#define PIN_EINK_RES (0 + 2)
#define PIN_EINK_SCK (0 + 31)
#define PIN_EINK_MOSI (0 + 29)

/*
 * Air530 GPS pins
 */

#define PIN_GPS_WAKE (32 + 4)
#define PIN_GPS_PPS (0 + 12)
#define PIN_GPS_TX (0 + 6) // This is for bits going TOWARDS the GPS
#define PIN_GPS_RX (0 + 8) // This is for bits going TOWARDS the CPU

#define PIN_SERIAL1_RX PIN_GPS_RX
#define PIN_SERIAL1_TX PIN_GPS_TX

/*
 * SPI Interfaces
 */
#define SPI_INTERFACES_COUNT 1

#define PIN_SPI_MISO (0 + 21)
#define PIN_SPI_MOSI (0 + 23)
#define PIN_SPI_SCK (0 + 19)

static const uint8_t SS = SX1262_CS;
static const uint8_t MOSI = PIN_SPI_MOSI;
static const uint8_t MISO = PIN_SPI_MISO;
static const uint8_t SCK = PIN_SPI_SCK;

// To debug via the segger JLINK console rather than the CDC-ACM serial device
#define USE_SEGGER

#ifdef __cplusplus
}
#endif

/*----------------------------------------------------------------------------
 *        Arduino objects - C++ only
 *----------------------------------------------------------------------------*/

#endif
