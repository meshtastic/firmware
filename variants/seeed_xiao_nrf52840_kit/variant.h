#ifndef _SEEED_XIAO_NRF52840_KIT_H_
#define _SEEED_XIAO_NRF52840_KIT_H_

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
#define NUM_ANALOG_INPUTS (8)
#define NUM_ANALOG_OUTPUTS (0)

/*
 * Digital Pins
 */
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

/*
 * LEDs
 */
#define LED_STATE_ON (0) // RGB LED is common anode
#define LED_RED (11)
#define LED_GREEN (13)
#define LED_BLUE (12)

#define PIN_LED1 LED_GREEN // PIN_LED1 is used in src/platform/nrf52/architecture.h to define LED_PIN
#define PIN_LED2 LED_BLUE
#define PIN_LED3 LED_RED

#define LED_BUILTIN LED_RED // LED_BUILTIN is used by framework-arduinoadafruitnrf52 to indicate flash writes

#define LED_PWR LED_RED
#define USER_LED LED_BLUE

/*
 * Buttons
 */

/*
 * D0 is shared with PIN_GPS_STANDBY on the L76K GNSS Module, so refer to
 * GPS_L76K definition preventing this conflict
 */

// #define BUTTON_PIN D0

/*
 * Serial Interfaces
 */
#define PIN_SERIAL2_RX (-1)
#define PIN_SERIAL2_TX (-1)

/*
 * Pinout for SX126x
 */
#define USE_SX1262

#ifdef XIAO_BLE_LEGACY_PINOUT
// Legacy xiao_ble variant pinout for third-party SX126x modules e.g. EBYTE E22
#define SX126X_CS D0
#define SX126X_DIO1 D1
#define SX126X_BUSY D2
#define SX126X_RESET D3
#define SX126X_RXEN D7

#elif defined(SEEED_XIAO_WIO_BTB)
// Wio-SX1262 for XIAO with 30-pin board-to-board connector
// https://files.seeedstudio.com/products/SenseCAP/Wio_SX1262/Schematic_Diagram_Wio-SX1262_for_XIAO.pdf
#define SX126X_CS D3
#define SX126X_DIO1 D0
#define SX126X_BUSY D1
#define SX126X_RESET D2
#define SX126X_RXEN D4
#else
// Wio-SX1262 for XIAO (standalone SKU 113010003 or nRF52840 kit SKU 102010710)
// https://files.seeedstudio.com/products/SenseCAP/Wio_SX1262/Wio-SX1262%20for%20XIAO%20V1.0_SCH.pdf
#define SX126X_CS D4
#define SX126X_DIO1 D1
#define SX126X_BUSY D3
#define SX126X_RESET D2
#define SX126X_RXEN D5
#endif

// Common pinouts for all SX126x pinouts above
#define SX126X_TXEN RADIOLIB_NC
#define SX126X_DIO2_AS_RF_SWITCH // DIO2 is used to control the TX side of the RF switch
#define SX126X_DIO3_TCXO_VOLTAGE 1.8

/*
 * SPI Interfaces
 * Defined after pinout for SX1262x to factor in CS pinout variations
 */
#define SPI_INTERFACES_COUNT 1

#define PIN_SPI_MISO D9
#define PIN_SPI_MOSI D10
#define PIN_SPI_SCK D8

static const uint8_t SS = SX126X_CS;
static const uint8_t MOSI = PIN_SPI_MOSI;
static const uint8_t MISO = PIN_SPI_MISO;
static const uint8_t SCK = PIN_SPI_SCK;

/*
 * GPS
 */
// GPS L76K
#ifdef GPS_L76K
#define PIN_GPS_RX D6
#define PIN_GPS_TX D7
#define HAS_GPS 1
#define GPS_THREAD_INTERVAL 50
#define PIN_SERIAL1_RX PIN_GPS_TX
#define PIN_SERIAL1_TX PIN_GPS_RX
#define PIN_GPS_STANDBY D0
#else
#define PIN_SERIAL1_RX (-1)
#define PIN_SERIAL1_TX (-1)
#endif

/*
 * Battery
 */
#define BATTERY_PIN PIN_VBAT      // P0.31: VBAT voltage divider
#define ADC_MULTIPLIER (3)        // ... R17=1M, R18=510k
#define ADC_CTRL VBAT_ENABLE      // P0.14: VBAT voltage divider
#define ADC_CTRL_ENABLED LOW      // ... sink
#define EXT_CHRG_DETECT (23)      // P0.17: Charge LED
#define EXT_CHRG_DETECT_VALUE LOW // ... BQ25101 ~CHG indicates charging
#define HICHG (22)                // P0.13: BQ25101 ISET 100mA instead of 50mA

#define BATTERY_SENSE_RESOLUTION_BITS (10)

/*
 * Wire Interfaces
 * Keep this section after potentially conflicting pin definitions
 */
#define I2C_NO_RESCAN // I2C is a bit finicky, don't scan too much
#define WIRE_INTERFACES_COUNT 1

#if defined(XIAO_BLE_LEGACY_PINOUT)
// Used for I2C by DIY xiao_ble variant
#define PIN_WIRE_SDA D4
#define PIN_WIRE_SCL D5
#elif !defined(GPS_L76K)
// If D6 and D7 are free, I2C is probably the most versatile assignment
#define PIN_WIRE_SDA D6
#define PIN_WIRE_SCL D7
#else
// Internal LSM6DS3TR on XIAO nRF52840 Series
#define PIN_WIRE_SDA (17)
#define PIN_WIRE_SCL (16)
#endif

static const uint8_t SDA = PIN_WIRE_SDA;
static const uint8_t SCL = PIN_WIRE_SCL;

/*
 * Buttons
 * Keep this section after potentially conflicting pin definitions
 * because D0 has multiple possible conflicts with various XIAO modules:
 * - PIN_GPS_STANDBY on the L76K GNSS Module
 * - DIO1 on the Wio-SX1262 - 30-pin board-to-board connector version
 * - SX1262X CS on XIAO BLE legacy pinout
 */

#if !defined(GPS_L76K) && !defined(SEEED_XIAO_WIO_BTB) && !defined(XIAO_BLE_LEGACY_PINOUT)
#define BUTTON_PIN D0
#endif

#ifdef __cplusplus
}
#endif

/*----------------------------------------------------------------------------
 *        Arduino objects - C++ only
 *----------------------------------------------------------------------------*/

#endif
