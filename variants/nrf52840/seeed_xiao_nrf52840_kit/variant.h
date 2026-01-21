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

/*
Xiao pin assignments

| Pin   | Default  | I2C  | BTB  | BLE-L |     | Pin   | Default | I2C  | BTB  | BLE-L |
| ----- | -------- | ---- | ---- | ----- | --- | ----- | ------- | ---- | ---- | ----- |
|       |          |      |      |       |     |       |         |      |      |       |
| D0    |          | UBTN | DIO1 | CS    |     | 5v    |         |      |      |       |
| D1    | DIO1     | DIO1 | Busy | DIO1  |     | GND   |         |      |      |       |
| D2    | NRST     | NRST | NRST | Busy  |     | 3v3   |         |      |      |       |
| D3    | Busy     | Busy | CS   | NRST  |     | D10   | MOSI    | MOSI | MOSI | MOSI  |
| D4    | CS       | CS   | RXEN | SDA   |     | D9    | MISO    | MISO | MISO | MISO  |
| D5    | RXEN     | RXEN |      | SCL   |     | D8    | SCK     | SCK  | SCK  | SCK   |
| D6    | G_TX     | SDA  | G_TX |       |     | D7    | G_RX    | SCL  | G_RX | RXEN  |
|       |          |      |      |       |     |       |         |      |      |       |
|       | End      |      |      |       |     |       |         |      |      |       |
| NFC1/ | SDA      | G_TX | SDA  | G_TX  |     | NFC2/ | SCL     | G_RX | SCL  | G_RX  |
| D30   |          |      |      |       |     | D31   |         |      |      |       |
|       |          |      |      |       |     |       |         |      |      |       |
|       | Internal |      |      |       |     |       |         |      |      |       |
| D16   | SCL1     | SCL1 | SCL1 | SCL1  |     |       |         |      |      |       |
| D17   | SDA1     | SDA1 | SDA1 | SDA1  |     |       |         |      |      |       |

The default column shows the pin assignments for the Wio-SX1262 for XIAO
(standalone SKU 113010003 or nRF52840 kit SKU 102010710).
The I2C column shows an alternative pin assignment using I2C on D6/D7 in place of the GNSS.
The BTB column shows the pin assignment for the Wio-SX1262 -30-pin board-to-board connector version from the ESP32S3 kit.
The BLE-L column shows the pin assignment for the original DIY xiao_ble, and which is retained for legacy users.
Note that the in addition to the difference between the default and the I2C pinouts in placing the pins on NFC or
D6/D7, the user button is activated on D0. The button conflicts with the official GNSS module, so caution is advised.
*/

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

#if defined(XIAO_BLE_LEGACY_PINOUT)
// Legacy xiao_ble variant pinout for third-party SX126x modules e.g. EBYTE E22
#define SX126X_CS D0
#define SX126X_DIO1 D1
#define SX126X_BUSY D2
#define SX126X_RESET D3
#define SX126X_RXEN D7
#else
#if defined(SEEED_XIAO_NRF_WIO_BTB)
// Wio-SX1262 for XIAO with 30-pin board-to-board connector
// https://files.seeedstudio.com/products/SenseCAP/Wio_SX1262/Schematic_Diagram_Wio-SX1262_for_XIAO.pdf
#define SX126X_CS D3
#define SX126X_DIO1 D0
#define SX126X_BUSY D1
#define SX126X_RESET D2
#define SX126X_RXEN D4
#else
// Wio-SX1262 for XIAO (standalone SKU 113010003 or nRF52840 kit SKU 102010710)
// Same for both default and I2C pinouts
// https://files.seeedstudio.com/products/SenseCAP/Wio_SX1262/Wio-SX1262%20for%20XIAO%20V1.0_SCH.pdf
#define SX126X_CS D4
#define SX126X_DIO1 D1
#define SX126X_BUSY D3
#define SX126X_RESET D2
#define SX126X_RXEN D5
#endif // defined(SEEED_XIAO_NRF_WIO_BTB)
#endif // defined(XIAO_BLE_LEGACY_PINOUT)

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

// Default GPS L76K
#if defined(SEEED_XIAO_NRF_KIT_DEFAULT) || defined(SEEED_XIAO_NRF_WIO_BTB)
#define GPS_L76K
#define GPS_TX_PIN D6 // This is data from the MCU
#define GPS_RX_PIN D7 // This is data from the GNSS module
#define PIN_GPS_STANDBY D0
// I2C and BLE-Legacy put them on the NFC pins
#else
#define GPS_TX_PIN (30)
#define GPS_RX_PIN (31)
#endif

#define HAS_GPS 1
#define GPS_BAUDRATE 9600
#define GPS_THREAD_INTERVAL 50
#define PIN_SERIAL1_TX GPS_TX_PIN
#define PIN_SERIAL1_RX GPS_RX_PIN

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
#define I2C_NO_RESCAN           // I2C is a bit finicky, don't scan too much
#define WIRE_INTERFACES_COUNT 1 // changed to 1 for now, as LSM6DS3TR has issues.

#if defined(XIAO_BLE_LEGACY_PINOUT)
// Used for I2C by DIY xiao_ble variant
#define PIN_WIRE_SDA D4
#define PIN_WIRE_SCL D5
#else
// Put the I2C pins on the NFC pins by default
#if defined(SEEED_XIAO_NRF_KIT_DEFAULT) || defined(SEEED_XIAO_NRF_WIO_BTB)
#define PIN_WIRE_SDA 30
#define PIN_WIRE_SCL 31
#else
// If not on legacy or defauly, we're wanting I2C on the back pins
#define PIN_WIRE_SDA D6
#define PIN_WIRE_SCL D7
#endif // defined(SEEED_XIAO_NRF_KIT_DEFAULT) || defined(SEEED_XIAO_NRF_WIO_BTB)
#endif // defined(XIAO_BLE_LEGACY_PINOUT)

// // Internal LSM6DS3TR on XIAO nRF52840 Series - put it on wire1
// // Note: disabled for now, as there are some issues with the LSM.
// #define PIN_WIRE1_SDA (17)
// #define PIN_WIRE1_SCL (16)

static const uint8_t SDA = PIN_WIRE_SDA; // Not sure if this is needed
static const uint8_t SCL = PIN_WIRE_SCL; // Not sure if this is needed

// // QSPI Pins
// // ---------
// #define PIN_QSPI_SCK (24)
// #define PIN_QSPI_CS (25)
// #define PIN_QSPI_IO0 (26)
// #define PIN_QSPI_IO1 (27)
// #define PIN_QSPI_IO2 (28)
// #define PIN_QSPI_IO3 (29)

// // On-board QSPI Flash
// // -------------------
// #define EXTERNAL_FLASH_DEVICES P25Q16H
// #define EXTERNAL_FLASH_USE_QSPI

/*
 * Buttons
 * Keep this section after potentially conflicting pin definitions
 * because D0 has multiple possible conflicts with various XIAO modules:
 */
#if defined(SEEED_XIAO_NRF_KIT_I2C)
#define BUTTON_PIN D0
#endif

#if defined(SEEED_XIAO_NRF_WIO_BTB)
#define BUTTON_PIN D5
#endif

#ifdef __cplusplus
}
#endif

/*----------------------------------------------------------------------------
 *        Arduino objects - C++ only
 *----------------------------------------------------------------------------*/

#endif