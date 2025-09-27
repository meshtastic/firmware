#ifndef _FOBE_QUILL_NRF52840_R1L_H_
#define _FOBE_QUILL_NRF52840_R1L_H_
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

// Use the native nrf52 usb power detection
#define NRF_APM

// Pin definitions
#define PINS_COUNT (48)
#define NUM_DIGITAL_PINS (48)
#define NUM_ANALOG_INPUTS (1) // A5 is used for battery
#define NUM_ANALOG_OUTPUTS (0)
#define SPI_SCK (0 + 20)
#define SPI_MOSI (0 + 22)
#define SPI_MISO (0 + 24)
#define PIN_BUILTIN_LED (32 + 11)
#define PIN_RESET (0 + 18)
#define PIN_I2C_SDA (0 + 7)
#define PIN_I2C_SCL (0 + 27)
#define PIN_PERI_EN (0 + 16)
#define PIN_ROTARY_ENCODER_A (32 + 6)
#define PIN_ROTARY_ENCODER_B (32 + 2)
#define PIN_ROTARY_ENCODER_S (32 + 4)
#define PIN_MOT_PWR (32 + 15)
#define PIN_MOT_INT (32 + 14)
#define PIN_MOT_SCL (32 + 13)
#define PIN_MOT_SDA (32 + 10)

/*
 * LEDs
 */
#define PIN_LED1 PIN_BUILTIN_LED
#define LED_RED PIN_LED1
#define LED_BLUE PIN_LED1
#define LED_GREEN PIN_LED1
#define LED_BUILTIN PIN_LED1
#define LED_STATE_ON 0

/*
 * Buttons
 */
#define PIN_BUTTON1 (32 + 0)
#define BUTTON_SENSE_TYPE INPUT_PULLUP_SENSE

/*
 * Battery
 */
#define BATTERY_PIN (0 + 5)
#define BATTERY_SENSE_RESOLUTION_BITS 12
#define BATTERY_SENSE_RESOLUTION 4096.0
#undef AREF_VOLTAGE
#define AREF_VOLTAGE 3.0
#define VBAT_AR_INTERNAL AR_INTERNAL_3_0
#define ADC_MULTIPLIER 1.73
#define EXT_CHRG_DETECT (32 + 12)
#define EXT_CHRG_DETECT_VALUE LOW

/*
 * Wire Interfaces
 */
#define HAS_WIRE 1
#define WIRE_INTERFACES_COUNT 2
#define I2C_NO_RESCAN
#define PIN_WIRE_SDA PIN_I2C_SDA
#define PIN_WIRE_SCL PIN_I2C_SCL
#define PIN_WIRE1_SDA PIN_MOT_SDA
#define PIN_WIRE1_SCL PIN_MOT_SCL
#define HAS_SCREEN 1
#define USE_SSD1306 1

/*
 * Serial interfaces
 */
#define PIN_SERIAL2_RX (-1)
#define PIN_SERIAL2_TX (-1)
#define PIN_SERIAL1_RX (0 + 12)
#define PIN_SERIAL1_TX (32 + 9)

/*
 * SPI Interfaces
 */
#define SPI_INTERFACES_COUNT 1
#define PIN_SPI_MISO SPI_MISO
#define PIN_SPI_MOSI SPI_MOSI
#define PIN_SPI_SCK SPI_SCK
static const uint8_t SS = (32 + 8);
static const uint8_t MOSI = PIN_SPI_MOSI;
static const uint8_t MISO = PIN_SPI_MISO;
static const uint8_t SCK = PIN_SPI_SCK;

/*
 * LoRa
 */
#define USE_SX1262
#define SX126X_CS (32 + 8)
#define SX126X_DIO1 (0 + 17)
#define SX126X_BUSY (0 + 15)
#define SX126X_RESET (0 + 13)
#define SX126X_TXEN RADIOLIB_NC
#define SX126X_RXEN (0 + 11)
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 1.8

/*
 * GNSS
 */
#define GPS_L76K
#define PIN_GPS_RX PIN_SERIAL1_TX
#define PIN_GPS_TX PIN_SERIAL1_RX
#define HAS_GPS 1
#define GPS_POWER_TOGGLE
#define GPS_BAUDRATE 9600
#define GPS_THREAD_INTERVAL 50
#define PIN_GPS_RESET (0 + 13)
#define GPS_RESET_MODE LOW
#define PIN_GPS_STANDBY (0 + 6)
#define PIN_GPS_EN (0 + 26)
#define PIN_GPS_PPS (0 + 8)
#define GPS_EN_ACTIVE HIGH
#define GPS_THREAD_INTERVAL 50

// Buzzer
#define PIN_BUZZER (0 + 14)

// Canned Messages
#define CANNED_MESSAGE_MODULE_ENABLE 1

#ifdef __cplusplus
}
#endif

/*----------------------------------------------------------------------------
 *        Arduino objects - C++ only
 *----------------------------------------------------------------------------*/

#endif
