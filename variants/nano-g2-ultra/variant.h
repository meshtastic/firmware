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

#ifndef _VARIANT_Nano_G2_
#define _VARIANT_Nano_G2_

/** Master clock frequency */
#define VARIANT_MCK (64000000ul)

#define USE_LFXO // Board uses 32khz crystal for LF
// #define USE_LFRC  // Board uses 32khz RC for LF

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
#define PIN_LED1 (-1)
#define PIN_LED2 (-1)
#define PIN_LED3 (-1)

#define LED_RED PIN_LED3
#define LED_BLUE PIN_LED1
#define LED_GREEN PIN_LED2

#define LED_BUILTIN LED_BLUE
#define LED_CONN PIN_GREEN

#define LED_STATE_ON 0 // State when LED is lit
// #define LED_INVERTED 1

/*
 * Buttons
 */
#define PIN_BUTTON1 (32 + 6)

#define EXT_NOTIFY_OUT (0 + 4) // Default pin to use for Ext Notify Module.

/*
 * Analog pins
 */
#define PIN_A4 (0 + 2) // Battery ADC

#define BATTERY_PIN PIN_A4

static const uint8_t A4 = PIN_A4;

#define ADC_RESOLUTION 14

/*
 * Serial interfaces
 */
#define PIN_SERIAL2_RX (0 + 22)
#define PIN_SERIAL2_TX (0 + 20)

/**
    Wire Interfaces
    */
#define WIRE_INTERFACES_COUNT 1

#define PIN_WIRE_SDA (0 + 17)
#define PIN_WIRE_SCL (0 + 15)

#define PIN_RTC_INT (0 + 14) // Interrupt from the PCF8563 RTC

/*
External serial flash W25Q16JV_IQ
*/

// QSPI Pins
#define PIN_QSPI_SCK (0 + 8)
#define PIN_QSPI_CS (32 + 7)
#define PIN_QSPI_IO0 (0 + 6)  // MOSI if using two bit interface
#define PIN_QSPI_IO1 (0 + 26) // MISO if using two bit interface
#define PIN_QSPI_IO2 (32 + 4) // WP if using two bit interface (i.e. not used)
#define PIN_QSPI_IO3 (32 + 2) // HOLD if using two bit interface (i.e. not used)

// On-board QSPI Flash
#define EXTERNAL_FLASH_DEVICES W25Q16JV_IQ
#define EXTERNAL_FLASH_USE_QSPI

/*
 * Lora radio
 */

#define USE_SX1262
#define SX126X_CS (32 + 13) // FIXME - we really should define LORA_CS instead
#define SX126X_DIO1 (32 + 10)
// Note DIO2 is attached internally to the module to an analog switch for TX/RX switching
// #define SX1262_DIO3 (0 + 21)
// This is used as an *output* from the sx1262 and connected internally to power the tcxo, do not drive from the main CPU?
#define SX126X_BUSY (32 + 11)
#define SX126X_RESET (32 + 15)
//  DIO2 controlls an antenna switch and the TCXO voltage is controlled by DIO3
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 1.8

// #define LORA_DISABLE_SENDING // Define this to disable transmission for testing (power testing etc...)

// #undef SX126X_CS

/*
 * GPS pins
 */

#define GPS_L76K

#define PIN_GPS_STANDBY (0 + 13) // An output to wake GPS, low means allow sleep, high means force wake STANDBY
#define PIN_GPS_TX (0 + 9)       // This is for bits going TOWARDS the CPU
#define PIN_GPS_RX (0 + 10)      // This is for bits going TOWARDS the GPS

// #define GPS_THREAD_INTERVAL 50

#define PIN_SERIAL1_RX PIN_GPS_TX
#define PIN_SERIAL1_TX PIN_GPS_RX

// PCF8563 RTC Module
#define PCF8563_RTC 0x51

/*
 * SPI Interfaces
 */
#define SPI_INTERFACES_COUNT 1

// For LORA, spi 0
#define PIN_SPI_MISO (32 + 9)
#define PIN_SPI_MOSI (0 + 11)
#define PIN_SPI_SCK (0 + 12)

// #define PIN_PWR_EN (0 + 6)

// To debug via the segger JLINK console rather than the CDC-ACM serial device
// #define USE_SEGGER

// Battery
// The battery sense is hooked to pin A0 (2)
// it is defined in the anlaolgue pin section of this file
// and has 12 bit resolution
#define BATTERY_SENSE_RESOLUTION_BITS 12
#define BATTERY_SENSE_RESOLUTION 4096.0
#undef AREF_VOLTAGE
#define AREF_VOLTAGE 3.0
#define VBAT_AR_INTERNAL AR_INTERNAL_3_0
#define ADC_MULTIPLIER (2.0F)

#define HAS_RTC 1

/**
    OLED Screen Model
    */
#define ARDUINO_ARCH_AVR
#define USE_SH1107_128_64

#ifdef __cplusplus
}
#endif

/*----------------------------------------------------------------------------
 *        Arduino objects - C++ only
 *----------------------------------------------------------------------------*/

#endif