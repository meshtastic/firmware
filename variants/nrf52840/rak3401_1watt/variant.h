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

#ifndef _VARIANT_RAK3401_
#define _VARIANT_RAK3401_

#define RAK4630

/** Master clock frequency */
#define VARIANT_MCK (64000000ul)

#define USE_LFXO // Board uses 32khz crystal for LF
// define USE_LFRC    // Board uses RC for LF

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
#define NUM_ANALOG_INPUTS (6)
#define NUM_ANALOG_OUTPUTS (0)

// LEDs
#define PIN_LED1 (35)
#define PIN_LED2 (36)

#define LED_BUILTIN PIN_LED1
#define LED_CONN PIN_LED2

#define LED_GREEN PIN_LED1
#define LED_BLUE PIN_LED2

#define LED_STATE_ON 1 // State when LED is litted

/*
 * Analog pins
 */
#define PIN_A0 (5)
#define PIN_A1 (31)
#define PIN_A2 (28)
#define PIN_A3 (29)
#define PIN_A4 (30)
#define PIN_A5 (31)
#define PIN_A6 (0xff)
#define PIN_A7 (0xff)

static const uint8_t A0 = PIN_A0;
static const uint8_t A1 = PIN_A1;
static const uint8_t A2 = PIN_A2;
static const uint8_t A3 = PIN_A3;
static const uint8_t A4 = PIN_A4;
static const uint8_t A5 = PIN_A5;
static const uint8_t A6 = PIN_A6;
static const uint8_t A7 = PIN_A7;
#define ADC_RESOLUTION 14

// Other pins
#define WB_I2C1_SDA (13) // SENSOR_SLOT IO_SLOT
#define WB_I2C1_SCL (14) // SENSOR_SLOT IO_SLOT

#define PIN_AREF (2)
#define PIN_NFC1 (9)
#define WB_IO5 PIN_NFC1
#define WB_IO4 (4)
#define PIN_NFC2 (10)

static const uint8_t AREF = PIN_AREF;

/*
 * Serial interfaces
 */
#define PIN_SERIAL1_RX (15)
#define PIN_SERIAL1_TX (16)

// Connected to Jlink CDC
#define PIN_SERIAL2_RX (8)
#define PIN_SERIAL2_TX (6)

/*
 * SPI Interfaces
 */
#define SPI_INTERFACES_COUNT 2

#define PIN_SPI_MISO (45)
#define PIN_SPI_MOSI (44)
#define PIN_SPI_SCK (43)

#define PIN_SPI1_MISO (29) // (0 + 29)
#define PIN_SPI1_MOSI (30) // (0 + 30)
#define PIN_SPI1_SCK (3)   // (0 + 3)

static const uint8_t SS = 42;
static const uint8_t MOSI = PIN_SPI_MOSI;
static const uint8_t MISO = PIN_SPI_MISO;
static const uint8_t SCK = PIN_SPI_SCK;

/*
 * eink display pins
 */
#define PIN_EINK_CS (0 + 26)
#define PIN_EINK_BUSY (0 + 4)
#define PIN_EINK_DC (0 + 17)
#define PIN_EINK_RES (-1)
#define PIN_EINK_SCLK (0 + 3)
#define PIN_EINK_MOSI (0 + 30) // also called SDI

/*
 * Wire Interfaces
 */
#define WIRE_INTERFACES_COUNT 1

#define PIN_WIRE_SDA (WB_I2C1_SDA)
#define PIN_WIRE_SCL (WB_I2C1_SCL)

// QSPI Pins
#define PIN_QSPI_SCK 3
#define PIN_QSPI_CS 26
#define PIN_QSPI_IO0 30
#define PIN_QSPI_IO1 29
#define PIN_QSPI_IO2 28
#define PIN_QSPI_IO3 2

// On-board QSPI Flash
#define EXTERNAL_FLASH_DEVICES IS25LP080D
#define EXTERNAL_FLASH_USE_QSPI

// 1watt sx1262 RAK13302
#define HW_SPI1_DEVICE 1

#define LORA_SCK PIN_SPI1_SCK
#define LORA_MISO PIN_SPI1_MISO
#define LORA_MOSI PIN_SPI1_MOSI
#define LORA_CS 26

#define USE_SX1262
#define SX126X_CS (26)
#define SX126X_DIO1 (10)
#define SX126X_BUSY (9)
#define SX126X_RESET (4)

#define SX126X_POWER_EN (21)
// DIO2 controlls an antenna switch and the TCXO voltage is controlled by DIO3
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 1.8

// Testing USB detection
#define NRF_APM
// If using a power chip like the INA3221 you can override the default battery voltage channel below
// and comment out NRF_APM to use the INA3221 instead of the USB detection for charging
// #define INA3221_BAT_CH INA3221_CH2
// #define INA3221_ENV_CH INA3221_CH1

// enables 3.3V periphery like GPS or IO Module
// Do not toggle this for GPS power savings
#define PIN_3V3_EN (34)
#define WB_IO2 PIN_3V3_EN

// RAK1910 GPS module
// If using the wisblock GPS module and pluged into Port A on WisBlock base
// IO1 is hooked to PPS (pin 12 on header) = gpio 17
// IO2 is hooked to GPS RESET = gpio 34, but it can not be used to this because IO2 is ALSO used to control 3V3_S power (1 is on).
// Therefore must be 1 to keep peripherals powered
// Power is on the controllable 3V3_S rail
// #define PIN_GPS_RESET (34)
// #define PIN_GPS_EN PIN_3V3_EN
#define PIN_GPS_PPS (17) // Pulse per second input from the GPS

#define GPS_RX_PIN PIN_SERIAL1_RX
#define GPS_TX_PIN PIN_SERIAL1_TX

// Define pin to enable GPS toggle (set GPIO to LOW) via user button triple press

// RAK12002 RTC Module
#define RV3028_RTC (uint8_t)0b1010010

// RAK18001 Buzzer in Slot C
// #define PIN_BUZZER 21 // IO3 is PWM2
// NEW: set this via protobuf instead!

// Battery
// The battery sense is hooked to pin A0 (5)
#define BATTERY_PIN PIN_A0
// and has 12 bit resolution
#define BATTERY_SENSE_RESOLUTION_BITS 12
#define BATTERY_SENSE_RESOLUTION 4096.0
#undef AREF_VOLTAGE
#define AREF_VOLTAGE 3.0
#define VBAT_AR_INTERNAL AR_INTERNAL_3_0
#define ADC_MULTIPLIER 1.73

#define HAS_RTC 1

#define RAK_4631 1

#ifdef __cplusplus
}
#endif

/*----------------------------------------------------------------------------
 *        Arduino objects - C++ only
 *----------------------------------------------------------------------------*/

#endif
