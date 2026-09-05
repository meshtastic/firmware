#ifndef _IKOKA_STICK_0_3_0_H_
#define _IKOKA_STICK_0_3_0_H_

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
// IKOKA STICK 0.3.0 user button
// D0 is available when GPS is not configured
#define BUTTON_PIN D0

/*
 * Serial Interfaces
 */
#define PIN_SERIAL2_RX (-1)
#define PIN_SERIAL2_TX (-1)

/*
 * Pinout for SX1268 (E22-400M33S module on IKOKA STICK 0.3.0)
 * Pin mapping based on IKOKA STICK 0.3.0 documentation:
 * - E22-DIO1: P0.03 (D1)
 * - E22-RST: P0.28 (D2)
 * - E22-BUSY: P0.29 (D3)
 * - E22-SPI NSS: P0.04 (D4)
 * - E22-RXEN: P0.05 (D5)
 * - SPI SCK: P1.13 (D8)
 * - SPI MISO: P1.14 (D9)
 * - SPI MOSI: P1.15 (D10)
 */
#define USE_SX1268 // E22-400M33S uses SX1268 chip

#define SX126X_CS D4      // P0.04: E22-SPI NSS
#define SX126X_DIO1 D1    // P0.03: E22-DIO1
#define SX126X_BUSY D3    // P0.29: E22-BUSY
#define SX126X_RESET D2   // P0.28: E22-RST
#define SX126X_RXEN D5    // P0.05: E22-RXEN

// E22-400M33S RF switch configuration:
// DIO2 is internally connected to TXEN on the E22 module
// RXEN is externally controlled via MCU pin D5
// This is the standard E22 configuration: DIO2→TXEN (internal), RXEN→MCU (external)
#define SX126X_TXEN RADIOLIB_NC // TXEN controlled by DIO2 internally
#define SX126X_DIO2_AS_RF_SWITCH // Enable DIO2 to control TXEN internally on E22 module
#define SX126X_DIO3_TCXO_VOLTAGE 2.2 // E22-M series: DIO3 outputs 2.2V to power 32MHz TCXO
#define TCXO_OPTIONAL // Enable TCXO support - firmware will try TCXO first, fallback to XTAL if needed

// Power configuration for E22-400M33S module with internal PA
// Based on RF output power comparison curve from E22-M Series User Manual:
// - Actual PA gain: 12 dB (21 dBm input → 33 dBm output per curve)
// - Virtual gain: 9 dB (used for scaling, not actual PA gain)
// - SX1268 max: 21 dBm (per curve, produces 33 dBm output)
// Scaling strategy: App's 0-30 dBm range maps to module's 0-33 dBm capability
// Example: User sets 30 dBm → Firmware: 30-9=21 dBm → PA: 21+12=33 dBm output
// This gives users access to full module power within app's 30 dBm limit
// Values (TX_GAIN_LORA=9, SX126X_MAX_POWER=21) defined in configuration.h

/*
 * SPI Interfaces
 * Defined after pinout for SX1268 to factor in CS pinout
 */
#define SPI_INTERFACES_COUNT 1

#define PIN_SPI_MISO D9  // P1.14: SPI MISO
#define PIN_SPI_MOSI D10 // P1.15: SPI MOSI
#define PIN_SPI_SCK D8   // P1.13: SPI SCK

static const uint8_t SS = SX126X_CS;
static const uint8_t MOSI = PIN_SPI_MOSI;
static const uint8_t MISO = PIN_SPI_MISO;
static const uint8_t SCK = PIN_SPI_SCK;

/*
 * GPS
 * IKOKA STICK 0.3.0 may have GPS, but not configured by default
 */
#define PIN_SERIAL1_RX (-1)
#define PIN_SERIAL1_TX (-1)

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
 * Display - SSD1306 OLED
 * IKOKA STICK 0.3.0 includes SSD1306 OLED display connected via I2C
 */
#define HAS_SCREEN 1
#define USE_SSD1306 1

/*
 * Wire Interfaces
 * Keep this section after potentially conflicting pin definitions
 */
#define I2C_NO_RESCAN // I2C is a bit finicky, don't scan too much
#define WIRE_INTERFACES_COUNT 1

// I2C pins - using D6 and D7 for I2C (shared with display and other I2C devices)
// Note: Internal I2C (D16/D17) is used for LSM6DS3TR sensor
#define PIN_WIRE_SDA D6  // P1.11: I2C SDA (for SSD1306 display)
#define PIN_WIRE_SCL D7  // P1.12: I2C SCL (for SSD1306 display)

static const uint8_t SDA = PIN_WIRE_SDA;
static const uint8_t SCL = PIN_WIRE_SCL;

/*
 * Buttons
 * Keep this section after potentially conflicting pin definitions
 */
// Button is already defined above, but ensure it's active
// D0 is used for button when GPS is not configured

#ifdef __cplusplus
}
#endif

/*----------------------------------------------------------------------------
 *        Arduino objects - C++ only
 *----------------------------------------------------------------------------*/

#endif
