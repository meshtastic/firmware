#ifndef _IKOKA_STICK_0_3_0_H_
#define _IKOKA_STICK_0_3_0_H_

/** Master clock frequency */
#define VARIANT_MCK (64000000ul)

#define USE_LFXO // Board uses 32 kHz crystal for LF

#include "WVariant.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PINS_COUNT (33)
#define NUM_DIGITAL_PINS (33)
#define NUM_ANALOG_INPUTS (8)
#define NUM_ANALOG_OUTPUTS (0)

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

#define LED_STATE_ON (0) // RGB LED is common anode
#define LED_RED (11)
#define LED_GREEN (13)
#define LED_BLUE (12)

#define PIN_LED1 LED_GREEN
#define PIN_LED2 LED_BLUE
#define PIN_LED3 LED_RED

#ifndef LED_BUILTIN
#define LED_BUILTIN LED_RED
#endif
#define LED_PWR LED_RED
#define USER_LED LED_BLUE

// IKOKA STICK 0.3.0 user button. D0 is available when GPS is not configured.
#define BUTTON_PIN D0

#define PIN_SERIAL2_RX (-1)
#define PIN_SERIAL2_TX (-1)

/*
 * Pinout for SX1268 (E22-400M33S module on IKOKA STICK 0.3.0)
 */
#define USE_SX1268

#define SX126X_CS D4
#define SX126X_DIO1 D1
#define SX126X_BUSY D3
#define SX126X_RESET D2
#define SX126X_RXEN D5

// E22-400M33S: DIO2 controls TXEN internally; RXEN is MCU-controlled.
#define SX126X_TXEN RADIOLIB_NC
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 2.2
#define TCXO_OPTIONAL

#define SPI_INTERFACES_COUNT 1

#define PIN_SPI_MISO D9
#define PIN_SPI_MOSI D10
#define PIN_SPI_SCK D8

static const uint8_t SS = SX126X_CS;
static const uint8_t MOSI = PIN_SPI_MOSI;
static const uint8_t MISO = PIN_SPI_MISO;
static const uint8_t SCK = PIN_SPI_SCK;

// GPS is not configured by default for this lab/test variant.
#define PIN_SERIAL1_RX (-1)
#define PIN_SERIAL1_TX (-1)

#define BATTERY_PIN PIN_VBAT
#define ADC_MULTIPLIER (3)
#define ADC_CTRL VBAT_ENABLE
#define ADC_CTRL_ENABLED LOW
#define EXT_CHRG_DETECT (23)
#define EXT_CHRG_DETECT_VALUE LOW
#define HICHG (22)

#define BATTERY_SENSE_RESOLUTION_BITS (10)

// IKOKA STICK 0.3.0 may include an SSD1306 OLED display.
#define HAS_SCREEN 1
#define USE_SSD1306 1

#define I2C_NO_RESCAN
#define WIRE_INTERFACES_COUNT 1

#define PIN_WIRE_SDA D6
#define PIN_WIRE_SCL D7

static const uint8_t SDA = PIN_WIRE_SDA;
static const uint8_t SCL = PIN_WIRE_SCL;

#ifdef __cplusplus
}
#endif

#endif
