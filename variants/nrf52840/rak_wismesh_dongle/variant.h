/*
  Copyright (c) 2014-2015 Arduino LLC.  All right reserved.
  Copyright (c) 2016 Sandeep Mistry All right reserved.
  Copyright (c) 2018, Adafruit Industries (adafruit.com)

  RAK10726 WisMesh Dongle: RAK4630 core + USB + LED1/LED2 + buzzer.
  Headless, no GPS, no battery divider, no user button (reset only).
*/

#ifndef _VARIANT_RAK4630_
#define _VARIANT_RAK4630_

#define RAK4630

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

// Number of pins defined in Pin Description array
#define PINS_COUNT (48)
#define NUM_DIGITAL_PINS (48)
#define NUM_ANALOG_INPUTS (6)
#define NUM_ANALOG_OUTPUTS (0)

// LEDs - schematic: LED1=P1.03, LED2=P1.04 (same as RAK4631 module LEDs)
#define PIN_LED1 (35)
#define LED_BLUE (36)

#define LED_GREEN PIN_LED1
#define LED_NOTIFICATION LED_BLUE

#define LED_STATE_ON 1 // State when LED is litted

/*
 * Analog pins (module pins present; no battery divider on this PCB)
 */
#define PIN_A0 (5)
#define PIN_A1 (31)
#define PIN_A2 (28)
#define PIN_A3 (29)
#define PIN_A4 (30)
#define PIN_A5 (31)
#define PIN_A6 (0xff)
#define PIN_A7 (0xff)

static const uint8_t A0 = PIN_A0;
static const uint8_t A1 = PIN_A1;
static const uint8_t A2 = PIN_A2;
static const uint8_t A3 = PIN_A3;
static const uint8_t A4 = PIN_A4;
static const uint8_t A5 = PIN_A5;
static const uint8_t A6 = PIN_A6;
static const uint8_t A7 = PIN_A7;
#define ADC_RESOLUTION 14

#define WB_I2C1_SDA (13)
#define WB_I2C1_SCL (14)

#define PIN_AREF (2)
#define PIN_NFC1 (9)
#define WB_IO5 PIN_NFC1
#define WB_IO4 (4)
#define PIN_NFC2 (10)

static const uint8_t AREF = PIN_AREF;

/*
 * Serial interfaces
 */
#define PIN_SERIAL1_RX (15)
#define PIN_SERIAL1_TX (16)

#define PIN_SERIAL2_RX (8)
#define PIN_SERIAL2_TX (6)

/*
 * SPI Interfaces
 */
#define SPI_INTERFACES_COUNT 2

#define PIN_SPI_MISO (45)
#define PIN_SPI_MOSI (44)
#define PIN_SPI_SCK (43)

#define PIN_SPI1_MISO (29)
#define PIN_SPI1_MOSI (30)
#define PIN_SPI1_SCK (3)

static const uint8_t SS = 42;
static const uint8_t MOSI = PIN_SPI_MOSI;
static const uint8_t MISO = PIN_SPI_MISO;
static const uint8_t SCK = PIN_SPI_SCK;

/*
 * eink pins - not populated on dongle, but required by einkScan.h when RAK_4631 is set
 */
#define PIN_EINK_CS (0 + 26)
#define PIN_EINK_BUSY (0 + 4)
#define PIN_EINK_DC (0 + 17)
#define PIN_EINK_RES (-1)
#define PIN_EINK_SCLK (0 + 3)
#define PIN_EINK_MOSI (0 + 30)

/*
 * Wire Interfaces
 */
#define WIRE_INTERFACES_COUNT 1

#define PIN_WIRE_SDA (WB_I2C1_SDA)
#define PIN_WIRE_SCL (WB_I2C1_SCL)

// QSPI Pins (on-module flash)
#define PIN_QSPI_SCK 3
#define PIN_QSPI_CS 26
#define PIN_QSPI_IO0 30
#define PIN_QSPI_IO1 29
#define PIN_QSPI_IO2 28
#define PIN_QSPI_IO3 2

#define EXTERNAL_FLASH_DEVICES IS25LP080D
#define EXTERNAL_FLASH_USE_QSPI

// SX1262 on RAK4630 module (same pin map as RAK4631)
#define USE_SX1262
#define SX126X_CS (42)
#define SX126X_DIO1 (47)
#define SX126X_BUSY (46)
#define SX126X_RESET (38)
#define SX126X_POWER_EN (37)
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 1.8

// USB VBUS detect via nRF POWER.USBREGSTATUS
#define NRF_APM

// Module pin P1.02 present; no WisBlock 3V3_S rail on this PCB - keep high if driven
#define PIN_3V3_EN (34)
#define WB_IO2 PIN_3V3_EN

// Buzzer - schematic: BUZZ = P0.21 (Arduino GPIO 21)
#define PIN_BUZZER 21

#define RAK_4631 1

#define HAS_SCREEN 0
#undef HAS_GPS
#define HAS_GPS 0
#undef HAS_BUTTON
#define HAS_BUTTON 0
#define HAS_ETHERNET 0

#ifdef __cplusplus
}
#endif

#endif
