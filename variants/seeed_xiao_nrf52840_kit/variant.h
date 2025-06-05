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
 *  D0 is shared with PIN_GPS_STANDBY on the L76K GNSS Module.
 *  There are some technical solutions that can solve this problem, and we are
 *  currently exploring and researching them.
 */

// #define BUTTON_PIN D0

/*
 * Serial Interfaces
 */
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

#define USE_SX1262

// Pinout for SX126X
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

// LSM6DS3TR on XIAO nRF52840 Series
#define PIN_WIRE_SDA (17)
#define PIN_WIRE_SCL (16)

static const uint8_t SDA = PIN_WIRE_SDA;
static const uint8_t SCL = PIN_WIRE_SCL;

/*
 * GPS
 */
// GPS L76KB
#define GPS_L76K
#ifdef GPS_L76K
#define PIN_GPS_RX D6
#define PIN_GPS_TX D7
#define HAS_GPS 1
#define GPS_BAUDRATE 9600
#define GPS_THREAD_INTERVAL 50
#define PIN_SERIAL1_RX PIN_GPS_TX
#define PIN_SERIAL1_TX PIN_GPS_RX
#define PIN_GPS_STANDBY D0
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

#ifdef __cplusplus
}
#endif

/*----------------------------------------------------------------------------
 *        Arduino objects - C++ only
 *----------------------------------------------------------------------------*/

#endif
