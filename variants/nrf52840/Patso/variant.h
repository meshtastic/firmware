#ifndef _VARIANT_PROMICRO_DIY_
#define _VARIANT_PROMICRO_DIY_

/** Master clock frequency */
#define VARIANT_MCK (64000000ul)

// #define USE_LFXO // Board uses 32khz crystal for LF
#define USE_LFRC // Board uses RC for LF

#define PROMICRO_DIY_XTAL
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
#define NUM_ANALOG_INPUTS (1)
#define NUM_ANALOG_OUTPUTS (0)

// Analog pins
#define PIN_3V3_EN (0 + 13) // P0.13
#define BATTERY_PIN (0 + 31) // P0.31 Battery ADC
#define ADC_CHANNEL ADC1_GPIO4_CHANNEL
#define ADC_RESOLUTION 14
#define BATTERY_SENSE_RESOLUTION_BITS 12
#define BATTERY_SENSE_RESOLUTION 4096.0
#define VBAT_MV_PER_LSB (0.73242188F)
#define VBAT_DIVIDER (0.6F)
#define VBAT_DIVIDER_COMP (1.73)
#define REAL_VBAT_MV_PER_LSB (VBAT_DIVIDER_COMP * VBAT_MV_PER_LSB)
#undef AREF_VOLTAGE
#define AREF_VOLTAGE 3.0
#define VBAT_AR_INTERNAL AR_INTERNAL_3_0
#define ADC_MULTIPLIER VBAT_DIVIDER_COMP // REAL_VBAT_MV_PER_LSB
#define VBAT_RAW_TO_SCALED(x) (REAL_VBAT_MV_PER_LSB * x)

// WIRE IC AND IIC PINS
#define WIRE_INTERFACES_COUNT 1
#define PIN_WIRE_SDA (32 + 4) // P1.04
#define PIN_WIRE_SCL (0 + 11) // P0.11

// LED
#define PIN_LED1 (0 + 15) // P0.15
#define LED_BUILTIN PIN_LED1
#define LED_BLUE PIN_LED1
#define LED_STATE_ON 1 // State when LED is lit

// Button
//#define BUTTON_PIN (32 + 0) // P1.00

// GPS
#define PIN_GPS_TX (0 + 22) // P0.22
#define PIN_GPS_RX (0 + 20) // P0.20
#define PIN_GPS_EN (0 + 24) // P0.24
#define GPS_POWER_TOGGLE
#define GPS_BAUD 9600


// UART interfaces
#define PIN_SERIAL1_RX PIN_GPS_TX
#define PIN_SERIAL1_TX PIN_GPS_RX
#define PIN_SERIAL2_RX -1
#define PIN_SERIAL2_TX -1


//#define CANNED_MESSAGE_MODULE_ENABLE 1

// trackball
#define HAS_TRACKBALL 1
#define TB_UP (0 + 6) // P0.06
#define TB_DOWN (0 + 8) // P0.08
#define TB_LEFT (32 + 2) //P1.02
#define TB_RIGHT (32 + 1) //P1.01
#define TB_PRESS (32 + 7) //P1.07
#define TB_DIRECTION FALLING

// Serial interfaces and LORA CONFIG
#define SPI_INTERFACES_COUNT 1
#define PIN_SPI_MISO (0 + 2)   // P0.02
#define PIN_SPI_MOSI (32 + 15) // P1.15
#define PIN_SPI_SCK (32 + 11)  // P1.11
#define USE_SX1262
#define SX126X_CS (32 + 13)      // P1.13 FIXME - we really should define LORA_CS instead
#define SX126X_DIO1 (0 + 10)     // P0.10 IRQ
#define SX126X_BUSY (0 + 29)     // P0.29
#define SX126X_RESET (0 + 9)     // P0.09

#ifdef __cplusplus
}
#endif

/*----------------------------------------------------------------------------
 *        Arduino objects - C++ only
 *----------------------------------------------------------------------------*/

#endif