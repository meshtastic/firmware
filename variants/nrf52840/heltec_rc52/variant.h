#ifndef _VARIANT_HELTEC_RC52_
#define _VARIANT_HELTEC_RC52_

#define VARIANT_MCK (64000000ul)
#define USE_LFXO // Board uses 32 kHz crystal for LF

#include "WVariant.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PINS_COUNT (48)
#define NUM_DIGITAL_PINS (48)
#define NUM_ANALOG_INPUTS (1)
#define NUM_ANALOG_OUTPUTS (0)

#define PIN_LED1 (0 + 15)
#define LED_POWER PIN_LED1
#define LED_BLUE (-1)
#define LED_GREEN PIN_LED1
#define LED_STATE_ON 1
#define LED_STATE_OFF (LED_STATE_ON ^ 1)

#define HAS_SCREEN 1
#define HAS_SPI_TFT 1
#define USE_TFTDISPLAY 1
#define USE_ARDUINO_GFX 1
#define TFT_NV3001B 1
#define TFT_NV3001B_DETECT 1
#define HAS_GPS 1
#define HAS_WIRE 1

/*
 * Optional RS-T108 / NV3001B TFT module on P2
 */
#define TFT_SCL (0 + 30)
#define TFT_SDA (32 + 2)
#define TFT_CS (32 + 4)
#define TFT_RS (0 + 28)
#define TFT_DC TFT_RS
#define TFT_RST (0 + 10)
#define TFT_EN (32 + 13)
#define TFT_EN_ON LOW
#define TFT_EN_OFF HIGH
#define VTFT_CTRL TFT_EN
#define TFT_BL (0 + 9)
#define TFT_BACKLIGHT_ON HIGH
#define TFT_BACKLIGHT_OFF LOW
#define TFT_WIDTH 128
#define TFT_HEIGHT 220
#define SPI_FREQUENCY 8000000
#define SCREEN_ROTATE

/*
 * Buttons
 */
#define PIN_BUTTON1 (32 + 10) // P1.10, external pull-up, active low

/*
 * Sensor I2C and control pins
 */
#define WIRE_INTERFACES_COUNT 1
#define PIN_WIRE_SDA (32 + 11) // P1.11
#define PIN_WIRE_SCL (0 + 2)   // P0.02
#define SENSOR_POWER_CTRL_PIN (0 + 12)
#define SENSOR_POWER_ON HIGH
#define SENSOR_INT (0 + 20)
#define SENSOR_RST (32 + 15)
#define HAS_TCA6408_ROTARY 1

/*
 * Serial interfaces
 */
#define GPS_RX_PIN (0 + 8)
#define GPS_TX_PIN (0 + 7)
#define PIN_GPS_EN (32 + 9)
#define GPS_EN_ACTIVE HIGH
#define PIN_GPS_PPS (32 + 1)
#define PIN_GPS_RESET (32 + 6)
#define GPS_RESET_MODE LOW
#define GPS_THREAD_INTERVAL 50
#define PERIPHERAL_WARMUP_MS 100

#define PIN_SERIAL1_RX GPS_RX_PIN
#define PIN_SERIAL1_TX GPS_TX_PIN
#define PIN_SERIAL2_RX (-1)
#define PIN_SERIAL2_TX (-1)

/*
 * LoRa radio
 */
#define USE_SX1262
#define SX126X_CS (0 + 13) // P0.13
#define LORA_CS SX126X_CS
#define SX126X_DIO1 (0 + 11)  // P0.11
#define SX126X_BUSY (0 + 24)  // P0.24
#define SX126X_RESET (32 + 0) // P1.00
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 1.8

#define RADIOCORE_FEM_EN (0 + 26)
#define RADIOCORE_VFEM_CTRL (0 + 16)

/*
 * SPI
 */
#define SPI_INTERFACES_COUNT 2
#define PIN_SPI_MISO (0 + 14)
#define PIN_SPI_MOSI (0 + 22)
#define PIN_SPI_SCK (0 + 25)

#define PIN_SPI1_MISO (-1)
#define PIN_SPI1_MOSI TFT_SDA
#define PIN_SPI1_SCK TFT_SCL

/*
 * Battery
 */
#define ADC_CTRL (0 + 4)
#define ADC_CTRL_ENABLED HIGH
#define BATTERY_PIN (0 + 31) // P0.31/AIN7
#define ADC_RESOLUTION 14

#define BATTERY_SENSE_RESOLUTION_BITS 12
#define BATTERY_SENSE_RESOLUTION 4096.0
#undef AREF_VOLTAGE
#define AREF_VOLTAGE 3.0
#define VBAT_AR_INTERNAL AR_INTERNAL_3_0
#define ADC_MULTIPLIER (4.9F)

#ifdef __cplusplus
}
#endif

#endif
