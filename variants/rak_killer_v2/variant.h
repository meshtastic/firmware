#ifndef _VARIANT_PROMICRO_DIY_
#define _VARIANT_PROMICRO_DIY_

/** Master clock frequency */
#define VARIANT_MCK (64000000ul)

#define USE_LFXO // Board uses 32khz crystal for LF
// #define USE_LFRC // Board uses RC for LF

#define PROMICRO_DIY_TCXO

/*----------------------------------------------------------------------------
 *        Headers
 *----------------------------------------------------------------------------*/

#include "WVariant.h"

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

/*
NRF52 PRO MICRO PIN ASSIGNMENT

| Pin   | Function   |   | Pin     | Function    |
|-------|------------|---|---------|-------------|
| Gnd   |            |   | vbat    |             |
| P0.06 | Serial2 RX |   | vbat    |             |
| P0.08 | Serial2 TX |   | Gnd     |             |
| Gnd   |            |   | reset   |             |
| Gnd   |            |   | ext_vcc | *see 0.13   |
| P0.17 | RXEN       |   | P0.31   | BATTERY_PIN |
| P0.20 | GPS_RX     |   | P0.29   | BUSY        |
| P0.22 | GPS_TX     |   | P0.02   | SCK         |
| P0.24 | GPS_EN     |   | P1.15   | MISO        |
| P1.00 | free pin   |   | P1.13   | MOSI        |
| P0.11 | free pin   |   | P1.11   | CS          |
| P1.04 | SCL        |   | P0.10   | RESET       |
| P1.06 | SDA        |   | P0.09   | DIO1        |
|       |            |   |         |             |
|       | Mid board  |   |         | Internal    |
| P1.01 | BUTTON_PIN |   | 0.15    | LED         |
| P1.02 | Free pin   |   | 0.13    | 3V3_EN      |
| P1.07 | Free pin   |   |         |             |
*/

// Number of pins defined in PinDescription array
#define PINS_COUNT (48)
#define NUM_DIGITAL_PINS (48)
#define NUM_ANALOG_INPUTS (1)
#define NUM_ANALOG_OUTPUTS (0)

// Pin 13 enables 3.3V periphery. If the Lora module is on this pin, then it should stay enabled at all times.
#define PIN_3V3_EN (0 + 13) // P0.13

// Analog pins
#define BATTERY_PIN (0 + 31) // P0.31 Battery ADC
#define ADC_RESOLUTION 14
#define BATTERY_SENSE_RESOLUTION_BITS 12
#define BATTERY_SENSE_RESOLUTION 4096.0
#undef AREF_VOLTAGE
#define AREF_VOLTAGE 3.0
#define VBAT_AR_INTERNAL AR_INTERNAL_3_0
#define ADC_MULTIPLIER (2.025F)

// WIRE IC AND IIC PINS
#define WIRE_INTERFACES_COUNT 1

#define PIN_WIRE_SDA (32 + 6) // P1.06
#define PIN_WIRE_SCL (32 + 4) // P1.04

// LED
#define PIN_LED1 (0 + 15) // P0.15
// #define PIN_LED2 (32 + 7) // P1.07
// #define PIN_LED3 (32 + 7) // P1.07
#define LED_BUILTIN PIN_LED1
#define LED_BLUE PIN_LED1 // LED is RED
#define LED_STATE_ON 1 // State when LED is lit


// Button
#define BUTTON_PIN (32 + 1) // P1.01

// GPS
#define PIN_GPS_TX (0 + 22) // P0.22
#define PIN_GPS_RX (0 + 20) // P0.20

#define PIN_GPS_EN (0 + 24) // P0.24
#define GPS_POWER_TOGGLE
#define GPS_UBLOX
// define GPS_DEBUG

// UART interfaces
#define PIN_SERIAL1_RX PIN_GPS_TX
#define PIN_SERIAL1_TX PIN_GPS_RX

#define PIN_SERIAL2_RX (0 + 6) // P0.06
#define PIN_SERIAL2_TX (0 + 8) // P0.08

// Serial interfaces
#define SPI_INTERFACES_COUNT 1

#define PIN_SPI_MISO (32 + 15)   // P1.15
#define PIN_SPI_MOSI (32 + 13) // P1.13
#define PIN_SPI_SCK (0 + 2)  // P0.02

// LORA MODULES
// #define USE_LLCC68
#define USE_SX1262
// #define USE_RF95
// #define USE_SX1268

// LORA CONFIG
#define SX126X_CS (32 + 11)      // P1.11 FIXME - we really should define LORA_CS instead
#define SX126X_DIO1 (0 + 9)     // P0.09 IRQ
#define SX126X_DIO2_AS_RF_SWITCH // Note for E22 modules: DIO2 is not attached internally to TXEN for automatic TX/RX switching,
                                 // so it needs connecting externally if it is used in this way
#define SX126X_BUSY (0 + 29)     // P0.29
#define SX126X_RESET (0 + 10)     // P0.10
#define SX126X_RXEN RADIOLIB_NC     // P0.17 sx1262 no connect
#define SX126X_TXEN RADIOLIB_NC  // Assuming that DIO2 is connected to TXEN pin. If not, TXEN must be connected.

// #define SX126X_MAX_POWER 8 set this if using a high-power board!

/*
On the SX1262, DIO3 sets the voltage for an external TCXO, if one is present. If one is not present, use TCXO_OPTIONAL to try both
settings.

| Mfr        | Module           | TCXO | RF Switch | Notes                                        |
| ---------- | ---------------- | ---- | --------- | -------------------------------------------- |
| Ebyte      | E22-900M22S      | Yes  | Ext       |                                              |
| Ebyte      | E22-900MM22S     | No   | Ext       |                                              |
| Ebyte      | E22-900M30S      | Yes  | Ext       |                                              |
| Ebyte      | E22-900M33S      | Yes  | Ext       | MAX_POWER must be set to 8 for this          |
| Ebyte      | E220-900M22S     | No   | Ext       | LLCC68, looks like DIO3 not connected at all |
| AI-Thinker | RA-01SH          | No   | Int       |                                              |
| Heltec     | HT-RA62          | Yes  | Int       |                                              |
| NiceRF     | Lora1262         | yes  | Int       |                                              |
| Waveshare  | Core1262-HF      | yes  | Ext       |                                              |
| Waveshare  | LoRa Node Module | yes  | Int       |                                              |

*/

#define SX126X_DIO3_TCXO_VOLTAGE 1.8
#define TCXO_OPTIONAL     // make it so that the firmware can try both TCXO and XTAL
extern float tcxoVoltage; // make this available everywhere

#ifdef __cplusplus
}
#endif

/*----------------------------------------------------------------------------
 *        Arduino objects - C++ only
 *----------------------------------------------------------------------------*/
