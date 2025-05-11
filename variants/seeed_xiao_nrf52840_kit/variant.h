#ifndef _SEEED_XIAO_NRF52840_SENSE_H_
#define _SEEED_XIAO_NRF52840_SENSE_H_

/** Master clock frequency */
#define VARIANT_MCK (64000000ul)

#define USE_LFXO // Board uses 32khz crystal for LF
// #define USE_LFRC    // Board uses RC for LF

/*----------------------------------------------------------------------------
 *        Headers
 *----------------------------------------------------------------------------*/

#include "WVariant.h"

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#define PINS_COUNT (33)
#define NUM_DIGITAL_PINS (33)
#define NUM_ANALOG_INPUTS (8) // A6 is used for battery, A7 is analog reference
#define NUM_ANALOG_OUTPUTS (0)

#ifdef SEEED_XIAO_D6D7_OLED
#define HAS_SCREEN 1
#define USE_SSD1306
#else
// Breaks build:
// #define HAS_SCREEN 0
#endif

// LEDs

#define LED_RED 11
#define LED_BLUE 12
#define LED_GREEN 13

#define PIN_LED1 LED_GREEN
#define PIN_LED2 LED_BLUE
#define PIN_LED3 LED_RED

#define PIN_LED PIN_LED1
#define LED_PWR (PINS_COUNT)

#define LED_BUILTIN PIN_LED

#define LED_STATE_ON 1 // State when LED is lit

/*
 * Buttons
 */

// Digital PINs
#define D0 (0ul)
#define D1 (1ul)
#define D2 (2ul)
#define D3 (3ul)
#define D4 (4ul)
#define D5 (5ul)
#define D6 (6ul)
#define D7 (7ul)
#define D8 (8ul)
#define D9 (9ul)
#define D10 (10ul)

// Wio-SX1262 for XIAO
// Store page: https://www.seeedstudio.com/Wio-SX1262-for-XIAO-p-6379.html
// Schematic: https://files.seeedstudio.com/products/SenseCAP/Wio_SX1262/Wio-SX1262%20for%20XIAO%20V1.0_SCH.pdf
#define PIN_BUTTON1 D0
#define BUTTON_NEED_PULLUP

/*
 * Analog pins
 */
#define PIN_A0 (0)
#define PIN_A1 (1)
#define PIN_A2 (32)
#define PIN_A3 (3)
#define PIN_A4 (4)
#define PIN_A5 (5)
#define PIN_VBAT (32)
#define VBAT_ENABLE (14)

static const uint8_t A0 = PIN_A0;
static const uint8_t A1 = PIN_A1;
static const uint8_t A2 = PIN_A2;
static const uint8_t A3 = PIN_A3;
static const uint8_t A4 = PIN_A4;
static const uint8_t A5 = PIN_A5;
#define ADC_RESOLUTION 12

// RX and TX pins
#if defined(SEEED_XIAO_D6D7_SERIAL1) || defined(SEEED_XIAO_D6D7_L76K)
#define PIN_SERIAL1_RX D6
#define PIN_SERIAL1_TX D7
#else
#define PIN_SERIAL1_RX (-1)
#define PIN_SERIAL1_TX (-1)
#endif

#define PIN_SERIAL2_RX (-1)
#define PIN_SERIAL2_TX (-1)

/*
 * SPI Interfaces
 */
#define SPI_INTERFACES_COUNT 1

#define PIN_SPI_MISO (9)
#define PIN_SPI_MOSI (10)
#define PIN_SPI_SCK (8)

static const uint8_t SS = D4;
static const uint8_t MOSI = PIN_SPI_MOSI;
static const uint8_t MISO = PIN_SPI_MISO;
static const uint8_t SCK = PIN_SPI_SCK;

// supported modules list
#define USE_SX1262

// common pinouts for SX126X modules

#define SX126X_CS D4
#define SX126X_DIO1 D1
#define SX126X_BUSY D3
#define SX126X_RESET D2

#define SX126X_TXEN RADIOLIB_NC

#define SX126X_RXEN D5           // This is used to control the RX side of the RF switch
#define SX126X_DIO2_AS_RF_SWITCH // DIO2 is used to control the TX side of the RF switch
#define SX126X_DIO3_TCXO_VOLTAGE 1.8

/*
 * Wire Interfaces
 */

#define I2C_NO_RESCAN           // I2C is a bit finicky, don't scan too much
#define WIRE_INTERFACES_COUNT 1 // 2

#if defined(SEEED_XIAO_D6D7_I2C) || defined(USE_SSD1306)
#define PIN_WIRE_SDA D6
#define PIN_WIRE_SCL D7
#else
// Invalid pins from original variant definition
#define PIN_WIRE_SDA (24)
#define PIN_WIRE_SCL (25)
#endif

static const uint8_t SDA = PIN_WIRE_SDA;
static const uint8_t SCL = PIN_WIRE_SCL;

// L76K GNSS Module for Seeed Studio XIAO
// Store page: https://www.seeedstudio.com/L76K-GNSS-Module-for-Seeed-Studio-XIAO-p-5864.html
// Schematic:
// https://files.seeedstudio.com/wiki/Seeeduino-XIAO-Expansion-Board/GPS_Module/L76K/109100021-L76K-GNSS-Module-for-Seeed-Studio-XIAO-Schematic.pdf
// -------------------
#ifdef SEEED_XIAO_D6D7_L76K
#define GPS_L76K
#define HAS_GPS 1
#define GPS_BAUDRATE 9600
#define GPS_THREAD_INTERVAL 50
#define PIN_GPS_STANDBY D0
#undef PIN_BUTTON1 // Overlapping assignment with Wio-SX1262 for XIAO user button
#define PIN_BUTTON1 (-1)
#else
#define HAS_GPS 0
#endif

// Battery

#define BAT_READ                                                                                                                 \
    14 // P0_14 = 14  Reads battery voltage from divider on signal board. (PIN_VBAT is reading voltage divider on XIAO and is
       // program pin 32 / or P0.31)
#define BATTERY_SENSE_RESOLUTION_BITS 10
#define CHARGE_LED 23 // P0_17 = 17  D23   YELLOW CHARGE LED
#define HICHG 22      // P0_13 = 13  D22   Charge-select pin for Lipo for 100 mA instead of default 50mA charge

// The battery sense is hooked to pin A0 (5)
#define BATTERY_PIN PIN_VBAT // PIN_A0

// ratio of voltage divider = 3.0 (R17=1M, R18=510k)
#define ADC_MULTIPLIER 3 // 3.0 + a bit for being optimistic

#ifdef __cplusplus
}
#endif

/*----------------------------------------------------------------------------
 *        Arduino objects - C++ only
 *----------------------------------------------------------------------------*/

#endif
