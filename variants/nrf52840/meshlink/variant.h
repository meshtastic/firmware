#ifndef _VARIANT_MESHLINK_
#define _VARIANT_MESHLINK_
#ifndef MESHLINK
#define MESHLINK
#endif
/** Master clock frequency */
#define VARIANT_MCK (64000000ul)

// #define USE_LFXO // Board uses 32khz crystal for LF
#define USE_LFRC // Board uses RC for LF

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
#define NUM_ANALOG_INPUTS (2)
#define NUM_ANALOG_OUTPUTS (0)

#define BUTTON_PIN (-1) // If defined, this will be used for user button presses,
#define BUTTON_NEED_PULLUP

// LEDs
#define PIN_LED1 (24) // Built in white led for status
#define LED_BLUE PIN_LED1
#define LED_BUILTIN PIN_LED1

#define LED_STATE_ON 0 // State when LED is litted
#define LED_INVERTED 1

// Testing USB detection
// #define NRF_APM

/*
 * Analog pins
 */
#define PIN_A1 (3) // P0.03/AIN1
#define ADC_RESOLUTION 14

// Other pins
// #define PIN_AREF (2)
// static const uint8_t AREF = PIN_AREF;

/*
 * Serial interfaces
 */
#define PIN_SERIAL1_RX (32 + 8)
#define PIN_SERIAL1_TX (7)

/*
 * SPI Interfaces
 */
#define SPI_INTERFACES_COUNT 2

#define PIN_SPI_MISO (8)
#define PIN_SPI_MOSI (32 + 9)
#define PIN_SPI_SCK (11)

#define PIN_SPI1_MISO (23)
#define PIN_SPI1_MOSI (21)
#define PIN_SPI1_SCK (19)

static const uint8_t SS = 12;
static const uint8_t MOSI = PIN_SPI_MOSI;
static const uint8_t MISO = PIN_SPI_MISO;
static const uint8_t SCK = PIN_SPI_SCK;

/*
 * eink display pins
 */
// #define USE_EINK

#define PIN_EINK_CS (15)
#define PIN_EINK_BUSY (16)
#define PIN_EINK_DC (14)
#define PIN_EINK_RES (17)
#define PIN_EINK_SCLK (19)
#define PIN_EINK_MOSI (21) // also called SDI

/*
 * Wire Interfaces
 */
#define WIRE_INTERFACES_COUNT 1

#define PIN_WIRE_SDA (1)
#define PIN_WIRE_SCL (27)

// QSPI Pins
#define PIN_QSPI_SCK 19
#define PIN_QSPI_CS 22
#define PIN_QSPI_IO0 21
#define PIN_QSPI_IO1 23
#define PIN_QSPI_IO2 32
#define PIN_QSPI_IO3 20

// On-board QSPI Flash
#define EXTERNAL_FLASH_DEVICES W25Q16JVUXIQ
#define EXTERNAL_FLASH_USE_QSPI

#define USE_SX1262
#define SX126X_CS (12)
#define SX126X_DIO1 (32 + 1)
#define SX126X_BUSY (32 + 3)
#define SX126X_RESET (6)
// #define SX126X_RXEN (13)
// DIO2 controlls an antenna switch and the TCXO voltage is controlled by DIO3
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 1.8

// pin 25 is used to enable or disable the watchdog. This pin has to be disabled when cpu is put to sleep
// otherwise the timer will expire and wd will reboot the cpu
#define PIN_WD_EN (25)

#define PIN_GPS_PPS (26) // Pulse per second input from the GPS

#define GPS_TX_PIN PIN_SERIAL1_RX // This is for bits going TOWARDS the CPU
#define GPS_RX_PIN PIN_SERIAL1_TX // This is for bits going TOWARDS the GPS

// #define GPS_THREAD_INTERVAL 50

// Define pin to enable GPS toggle (set GPIO to LOW) via user button triple press
#define PIN_GPS_EN (0)
#define GPS_EN_ACTIVE LOW

#define PIN_BUZZER (31) // P0.31/AIN7

// Battery
// The battery sense is hooked to pin A0 (2)
#define BATTERY_PIN (2)
// and has 12 bit resolution
#define BATTERY_SENSE_RESOLUTION_BITS 12
#define BATTERY_SENSE_RESOLUTION 4096.0
#undef AREF_VOLTAGE
#define AREF_VOLTAGE 3.0
#define VBAT_AR_INTERNAL AR_INTERNAL_3_0
#define ADC_MULTIPLIER 1.42 // fine tuning of voltage

#ifdef __cplusplus
}
#endif

/*----------------------------------------------------------------------------
 *        Arduino objects - C++ only
 *----------------------------------------------------------------------------*/
#endif