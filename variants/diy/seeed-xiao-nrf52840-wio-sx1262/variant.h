// basically xiao_ble with pins remapped for:
// Seeed XIAO nRF52840 : https://www.seeedstudio.com/Seeed-XIAO-BLE-nRF52840-p-5201.html
// Seeed Wio SX1626 : https://www.seeedstudio.com/Wio-SX1262-with-XIAO-ESP32S3-p-5982.html

#ifndef _SEEED_XIAO_NRF52840_SENSE_H_
#define _SEEED_XIAO_NRF52840_SENSE_H_

/** Master clock frequency */
#define VARIANT_MCK (64000000ul)

#define USE_LFXO // Board uses 32khz crystal for LF

/*----------------------------------------------------------------------------
 *        Headers
 *----------------------------------------------------------------------------*/

#include "WVariant.h"

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#define PINS_COUNT (33)
#define NUM_DIGITAL_PINS (33)
#define NUM_ANALOG_INPUTS (8) // A6 is used for battery, A7 is analog reference
#define NUM_ANALOG_OUTPUTS (0)

// LEDs
// ----
#define LED_RED 11
#define LED_BLUE 12
#define LED_GREEN 13

#define PIN_LED1 LED_GREEN
#define PIN_LED2 LED_BLUE
#define PIN_LED3 LED_RED

#define PIN_LED PIN_LED1
#define LED_PWR (PINS_COUNT)

#define LED_BUILTIN PIN_LED
#define LED_STATE_ON 1 // State when LED is lit

// XIAO Wio-SX1262 Shield User button
#define PIN_BUTTON1 5
#define BUTTON_NEED_PULLUP

// Digital Pins
// ------------
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

// Analog Pins
// -----------
#define PIN_A0 (0)
#define PIN_A1 (1)
#define PIN_A2 (2)
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

// Other Pins
// ----------
#define PIN_NFC1 (30)
#define PIN_NFC2 (31)

// RX and TX pins
#define PIN_SERIAL1_RX (-1)
#define PIN_SERIAL1_TX (-1)
// complains if not defined
#define PIN_SERIAL2_RX (-1)
#define PIN_SERIAL2_TX (-1)

// 4 is used as RF_SW and 5 for USR button so...
#define PIN_WIRE_SDA (6)
#define PIN_WIRE_SCL (7)

static const uint8_t SDA = PIN_WIRE_SDA;
static const uint8_t SCL = PIN_WIRE_SCL;

// SPI SX1262
// ----------
#define SPI_SX1262
#ifdef SPI_SX1262
#define SPI_INTERFACES_COUNT 1

#define PIN_SPI_MISO (9)
#define PIN_SPI_MOSI (10)
#define PIN_SPI_SCK (8)

static const uint8_t SS = D3;
static const uint8_t MOSI = PIN_SPI_MOSI;
static const uint8_t MISO = PIN_SPI_MISO;
static const uint8_t SCK = PIN_SPI_SCK;

// supported modules list
#define USE_SX1262

// common pinouts for SX126X modules
#define SX126X_CS D3
#define SX126X_DIO1 D0
#define SX126X_BUSY D1
#define SX126X_RESET D2

// DIO2 controlls an antenna switch and the TCXO voltage is controlled by DIO3
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_RXEN 38
#define SX126X_TXEN RADIOLIB_NC
#define SX126X_DIO3_TCXO_VOLTAGE 1.8
#define SX126X_DIO3_TCXO_VOLTAGE 1.8
#endif

// Wire Interfaces
// -------------------
#define WIRE_INTERFACES_COUNT 1 // 2

// Sense version has IMU and PDM Mic
// #define XIAO_SENSE
#ifndef XIAO_SENSE
// 6 DoF IMU
#define PIN_LSM6DS3TR_C_POWER (15)
#define PIN_LSM6DS3TR_C_INT1 (18)
// PDM Interfaces
// ---------------
#define PIN_PDM_PWR (19)
#define PIN_PDM_CLK (20)
#define PIN_PDM_DIN (21)
#endif

// QSPI Pins
// ---------
#define PIN_QSPI_SCK (24)
#define PIN_QSPI_CS (25)
#define PIN_QSPI_IO0 (26)
#define PIN_QSPI_IO1 (27)
#define PIN_QSPI_IO2 (28)
#define PIN_QSPI_IO3 (29)

// On-board QSPI Flash
// -------------------
#define EXTERNAL_FLASH_DEVICES P25Q16H
#define EXTERNAL_FLASH_USE_QSPI

// Battery
// -------
// P0_14 = 14  Reads battery voltage from divider on signal board.
// PIN_VBAT is reading voltage divider on XIAO and is program pin 32 / or P0.31
#define BAT_READ 14
#define BATTERY_SENSE_RESOLUTION_BITS 10
#define CHARGE_LED 23 // P0_17 = 17  D23   YELLOW CHARGE LED
#define HICHG 22      // P0_13 = 13  D22   Charge-select pin for Lipo for 100 mA instead of default 50mA charge

// The battery sense is hooked to pin A0 (5)
#define BATTERY_PIN PIN_VBAT // PIN_A0

// ratio of voltage divider = 3.0 (R17=1M, R18=510k)
#define ADC_MULTIPLIER 3 // 3.0 + a bit for being optimistic

#ifdef __cplusplus
}
#endif

/*----------------------------------------------------------------------------
 *        Arduino objects - C++ only
 *----------------------------------------------------------------------------*/

#endif