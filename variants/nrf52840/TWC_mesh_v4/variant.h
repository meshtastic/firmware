#ifndef _VARIANT_TWC_MESH_V4_
#define _VARIANT_TWC_MESH_V4_

#define PCA10059

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
#define PIN_LED1 (32 + 10) // Blue LED        P1.10
#define PIN_LED2 (32 + 15) // Built in Green  P1.15

// RGB NeoPixel LED2
// #define PIN_LED1 (0 + 8) Red
// #define PIN_LED1 (32 + 9) Green
// #define PIN_LED1 (0 + 12) Blue

#define LED_BUILTIN PIN_LED1
#define LED_CONN PIN_LED2

#define LED_GREEN PIN_LED1
#define LED_BLUE PIN_LED2

#define LED_STATE_ON 0 // State when LED is litted

/*
 * Buttons
 */
#define PIN_BUTTON1 (32 + 2) // BTN_DN           P1.02 Built in button

/*
 * Analog pins
 */
#define PIN_A0 (0 + 29) // using VDIV (A6 / P0.29)

static const uint8_t A0 = PIN_A0;
#define ADC_RESOLUTION 14

// Other pins
#define PIN_AREF (-1) // AREF            Not yet used

static const uint8_t AREF = PIN_AREF;

/*
 * Serial interfaces
 */
#define PIN_SERIAL1_RX (0 + 24)
#define PIN_SERIAL1_TX (0 + 25)

// Connected to Jlink CDC
#define PIN_SERIAL2_RX (-1)
#define PIN_SERIAL2_TX (-1)

/*
 * SPI Interfaces
 */
#define SPI_INTERFACES_COUNT 1

#define PIN_SPI_MISO (0 + 15) // MISO      P0.15
#define PIN_SPI_MOSI (0 + 13) // MOSI      P0.13
#define PIN_SPI_SCK (0 + 14)  // SCK       P0.14

static const uint8_t SS = (0 + 6); // LORA_CS   P0.6
static const uint8_t MOSI = PIN_SPI_MOSI;
static const uint8_t MISO = PIN_SPI_MISO;
static const uint8_t SCK = PIN_SPI_SCK;

////#define USE_EINK
#define USE_SSD1306

/*
 * Wire Interfaces
 */
#define WIRE_INTERFACES_COUNT 1

#define PIN_WIRE_SDA (0 + 12) // SDA     P0.12
#define PIN_WIRE_SCL (0 + 11) // SCL     P0.11

// NiceRF 868 LoRa module
#define USE_SX1262
#define USE_LLCC68

#define SX126X_CS (0 + 6)     // LORA_CS     P0.06
#define SX126X_DIO1 (0 + 7)   // DIO1        P0.07
#define SX126X_BUSY (0 + 26)  // LORA_BUSY	  P0.26
#define SX126X_RESET (0 + 27) // LORA_RESET  P0.27
#define SX126X_DIO3_TCXO_VOLTAGE 1.8

#define PIN_GPS_EN (-1)
#define PIN_GPS_PPS (-1) // Pulse per second input from the GPS

#define GPS_RX_PIN PIN_SERIAL1_RX
#define GPS_TX_PIN PIN_SERIAL1_TX

// Battery
// The battery sense is hooked to pin A6 (0.29)
#define BATTERY_PIN PIN_A0
// and has 12 bit resolution
#define BATTERY_SENSE_RESOLUTION_BITS 12
#define BATTERY_SENSE_RESOLUTION 4096.0
#undef AREF_VOLTAGE
#define AREF_VOLTAGE 3.0
#define VBAT_AR_INTERNAL AR_INTERNAL_3_0
#define ADC_MULTIPLIER (2.0F)

#ifdef __cplusplus
}
#endif

/*----------------------------------------------------------------------------
 *        Arduino objects - C++ only
 *----------------------------------------------------------------------------*/

#endif