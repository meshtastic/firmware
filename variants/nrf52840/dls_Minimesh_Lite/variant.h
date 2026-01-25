#ifndef _VARIANT_MINIMESH_LITE_
#define _VARIANT_MINIMESH_LITE_

#define VARIANT_MCK (64000000ul)
#define USE_LFRC

#include "WVariant.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MINIMESH_LITE

// Number of pins defined in PinDescription array
#define PINS_COUNT (48)
#define NUM_DIGITAL_PINS (48)
#define NUM_ANALOG_INPUTS (1)
#define NUM_ANALOG_OUTPUTS (0)

#define PIN_3V3_EN (0 + 13) // P0.13

// Analog pins
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
#define ADC_MULTIPLIER VBAT_DIVIDER_COMP
#define VBAT_RAW_TO_SCALED(x) (REAL_VBAT_MV_PER_LSB * x)

// WIRE IC AND IIC PINS
#define WIRE_INTERFACES_COUNT 1

#define PIN_WIRE_SDA (32 + 4)
#define PIN_WIRE_SCL (0 + 11)

// LED
#define PIN_LED1 (0 + 15)
#define LED_BUILTIN PIN_LED1
// Actually red
#define LED_BLUE PIN_LED1
#define LED_STATE_ON 1

// Button
#define BUTTON_PIN (32 + 0)

// GPS
#define GPS_TX_PIN (0 + 20)
#define GPS_RX_PIN (0 + 22)

#define PIN_GPS_EN (0 + 24)
#define GPS_UBLOX
// define GPS_DEBUG

// UART interfaces
#define PIN_SERIAL1_TX GPS_TX_PIN
#define PIN_SERIAL1_RX GPS_RX_PIN

#define PIN_SERIAL2_RX (0 + 6)
#define PIN_SERIAL2_TX (0 + 8)

// Serial interfaces
#define SPI_INTERFACES_COUNT 1

#define PIN_SPI_MISO (0 + 2)
#define PIN_SPI_MOSI (32 + 15)
#define PIN_SPI_SCK (32 + 11)

#define LORA_MISO PIN_SPI_MISO
#define LORA_MOSI PIN_SPI_MOSI
#define LORA_SCK PIN_SPI_SCK
#define LORA_CS (32 + 13)

// LORA MODULES
#define USE_LLCC68
#define USE_SX1262
#define USE_SX1268

// SX126X CONFIG
#define SX126X_CS (32 + 13)
#define SX126X_DIO1 (0 + 10)
#define SX126X_DIO2_AS_RF_SWITCH

#define SX126X_BUSY (0 + 29)
#define SX126X_RESET (0 + 9)
#define SX126X_RXEN (0 + 17)
#define SX126X_TXEN RADIOLIB_NC

#define SX126X_DIO3_TCXO_VOLTAGE 1.8
#define TCXO_OPTIONAL // make it so that the firmware can try both TCXO and XTAL

#ifdef __cplusplus
}
#endif

#endif // _VARIANT_MINIMESH_LITE_
