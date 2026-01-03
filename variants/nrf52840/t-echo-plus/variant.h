#ifndef _VARIANT_T_ECHO_PLUS_
#define _VARIANT_T_ECHO_PLUS_

#define VARIANT_MCK (64000000ul)
#define USE_LFXO

#include "WVariant.h"

#ifdef __cplusplus
extern "C" {
#endif

// Pin counts
#define PINS_COUNT (48)
#define NUM_DIGITAL_PINS (48)
#define NUM_ANALOG_INPUTS (1)
#define NUM_ANALOG_OUTPUTS (0)

// LEDs (not documented on pinmap; keep defaults for compatibility)
#define PIN_LED1 (0 + 14)
#define PIN_LED2 (0 + 15)
#define PIN_LED3 (0 + 13)

#define LED_RED PIN_LED3
#define LED_BLUE PIN_LED1
#define LED_GREEN PIN_LED2

#define LED_BUILTIN LED_BLUE
#define LED_CONN LED_GREEN

#define LED_STATE_ON 0

// Buttons / touch
#define PIN_BUTTON1 (32 + 10)
#define BUTTON_ACTIVE_LOW true
#define BUTTON_ACTIVE_PULLUP true
#define PIN_BUTTON2 (0 + 18)      // reset-labelled but usable as GPIO
#define PIN_BUTTON_TOUCH (0 + 11) // capacitive touch
#define BUTTON_TOUCH_ACTIVE_LOW true
#define BUTTON_TOUCH_ACTIVE_PULLUP true

#define BUTTON_CLICK_MS 400
#define BUTTON_TOUCH_MS 200

// Analog
#define PIN_A0 (4)
#define BATTERY_PIN PIN_A0
static const uint8_t A0 = PIN_A0;
#define ADC_RESOLUTION 14
#define BATTERY_SENSE_RESOLUTION_BITS 12
#define BATTERY_SENSE_RESOLUTION 4096.0
#undef AREF_VOLTAGE
#define AREF_VOLTAGE 3.0
#define VBAT_AR_INTERNAL AR_INTERNAL_3_0
#define ADC_MULTIPLIER (2.0F)

// NFC
#define PIN_NFC1 (9)
#define PIN_NFC2 (10)

// I2C (IMU BHI260AP, RTC, etc.)
#define WIRE_INTERFACES_COUNT 1
#define PIN_WIRE_SDA (0 + 26)
#define PIN_WIRE_SCL (0 + 27)
#define HAS_BHI260AP

#define TP_SER_IO (0 + 11)

// RTC interrupt
#define PIN_RTC_INT (0 + 16)

// QSPI flash
#define PIN_QSPI_SCK (32 + 14)
#define PIN_QSPI_CS (32 + 15)
#define PIN_QSPI_IO0 (32 + 12)
#define PIN_QSPI_IO1 (32 + 13)
#define PIN_QSPI_IO2 (0 + 7)
#define PIN_QSPI_IO3 (0 + 5)

// On-board QSPI Flash
#define EXTERNAL_FLASH_DEVICES MX25R1635F
#define EXTERNAL_FLASH_USE_QSPI

// LoRa SX1262
#define USE_SX1262
#define USE_SX1268
#define SX126X_CS (0 + 24)
#define SX126X_DIO1 (0 + 20)
#define SX1262_DIO3 (0 + 21)
#define SX126X_BUSY (0 + 17)
#define SX126X_RESET (0 + 25)
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 1.8
#define TCXO_OPTIONAL

#define SPI_INTERFACES_COUNT 2

#define PIN_SPI_MISO (0 + 23)
#define PIN_SPI_MOSI (0 + 22)
#define PIN_SPI_SCK (0 + 19)

// E-paper (1.54" per pinmap)
// Alias PIN_EINK_EN to keep common eink power control code working
#define PIN_EINK_BL (32 + 11) // backlight / panel power switch
#define PIN_EINK_EN PIN_EINK_BL
#define PIN_EINK_CS (0 + 30)
#define PIN_EINK_BUSY (0 + 3)
#define PIN_EINK_DC (0 + 28)
#define PIN_EINK_RES (0 + 2)
#define PIN_EINK_SCLK (0 + 31)
#define PIN_EINK_MOSI (0 + 29) // also called SDI

// Power control
#define PIN_POWER_EN (0 + 12)

#define PIN_SPI1_MISO (32 + 7) // Placeholder MISO; keep off QSPI pins to avoid contention
#define PIN_SPI1_MOSI PIN_EINK_MOSI
#define PIN_SPI1_SCK PIN_EINK_SCLK

// GPS (TX/RX/Wake/Reset/PPS per pinmap)
#define GPS_L76K
#define PIN_GPS_REINIT (32 + 5)  // Reset
#define PIN_GPS_STANDBY (32 + 2) // Wake
#define PIN_GPS_PPS (32 + 4)
#define GPS_TX_PIN (32 + 8)
#define GPS_RX_PIN (32 + 9)
#define GPS_THREAD_INTERVAL 50

#define PIN_SERIAL1_RX GPS_RX_PIN
#define PIN_SERIAL1_TX GPS_TX_PIN

// Sensors / accessories
#define PIN_BUZZER (0 + 6)
#define PIN_DRV_EN (0 + 8)

#define HAS_DRV2605 1

// Battery / ADC already defined above
#define HAS_RTC 1

#ifdef __cplusplus
}
#endif

#endif
