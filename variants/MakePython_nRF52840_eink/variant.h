#ifndef _VARIANT_MAKERPYTHON_NRF82540_EINK_
#define _VARIANT_MAKERPYTHON_NRF82540_EINK_

#define MAKERPYTHON

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

#define RADIOLIB_GODMODE

// LEDs
#define PIN_LED1 (32 + 10) // LED        P1.15
#define PIN_LED2 (-1)      //

#define LED_BUILTIN PIN_LED1
#define LED_CONN PIN_LED2

#define LED_GREEN PIN_LED1
#define LED_BLUE PIN_LED2

#define LED_STATE_ON 0 // State when LED is litted

/*
 * Buttons
 */

#define PIN_BUTTON1 (32 + 15) // P1.15 Built in button

/*
 * Analog pins
 */
#define PIN_A0 (-1)

static const uint8_t A0 = PIN_A0;
#define ADC_RESOLUTION 14

// Other pins
#define PIN_AREF (-1) // AREF            Not yet used

static const uint8_t AREF = PIN_AREF;

/*
 * Serial interfaces
 */
#define PIN_SERIAL1_RX (-1)
#define PIN_SERIAL1_TX (-1)

// Connected to Jlink CDC
#define PIN_SERIAL2_RX (-1)
#define PIN_SERIAL2_TX (-1)

/*
 * SPI Interfaces
 */
#define SPI_INTERFACES_COUNT 2
// here
// #define SPI_INTERFACES_COUNT 1

#define PIN_SPI_MISO (0 + 31) // MISO      P0.31
#define PIN_SPI_MOSI (0 + 30) // MOSI      P0.30
#define PIN_SPI_SCK (0 + 29)  // SCK       P0.29

// here
#define PIN_SPI1_MISO (-1)     //
#define PIN_SPI1_MOSI (0 + 28) // EPD_MOSI  P0.10
#define PIN_SPI1_SCK (0 + 2)   // EPD_SCLK  P0.09

static const uint8_t SS = (32 + 15); // LORA_CS   P1.15
static const uint8_t MOSI = PIN_SPI_MOSI;
static const uint8_t MISO = PIN_SPI_MISO;
static const uint8_t SCK = PIN_SPI_SCK;

// here
/*
 * eink display pins
 */

// #define PIN_EINK_EN    (-1)
#define PIN_EINK_CS (0 + 3)     // EPD_CS
#define PIN_EINK_BUSY (32 + 11) // EPD_BUSY
#define PIN_EINK_DC (32 + 13)   // EPD_D/C
#define PIN_EINK_RES (-1)       // Not used
#define PIN_EINK_SCLK (0 + 2)   // EPD_SCLK
#define PIN_EINK_MOSI (0 + 28)  // EPD_MOSI

#define USE_EINK

/*
 * Wire Interfaces
 */
#define WIRE_INTERFACES_COUNT 1

#define PIN_WIRE_SDA (0 + 21) // SDA
#define PIN_WIRE_SCL (0 + 22) // SCL

// E-Byte E28 2.4 Ghz LoRa module
#define USE_SX1280
#define LORA_RESET (0 + 5)
#define SX128X_CS (0 + 23)
#define SX128X_DIO1 (0 + 4)
#define SX128X_BUSY (0 + 7)
// #define SX128X_TXEN  (32 + 9)
// #define SX128X_RXEN  (0 + 12)
#define SX128X_RESET LORA_RESET

#define PIN_GPS_EN (-1)
#define PIN_GPS_PPS (-1) // Pulse per second input from the GPS

#define GPS_RX_PIN PIN_SERIAL1_RX
#define GPS_TX_PIN PIN_SERIAL1_TX

// Battery
// The battery sense is hooked to pin A0 (5)
#define BATTERY_PIN PIN_A0
// and has 12 bit resolution
#define BATTERY_SENSE_RESOLUTION_BITS 12
#define BATTERY_SENSE_RESOLUTION 4096.0
#undef AREF_VOLTAGE
#define AREF_VOLTAGE 3.0
#define VBAT_AR_INTERNAL AR_INTERNAL_3_0
#define ADC_MULTIPLIER (1.73F)

#ifdef __cplusplus
}
#endif

/*----------------------------------------------------------------------------
 *        Arduino objects - C++ only
 *----------------------------------------------------------------------------*/

#endif