#ifndef _SEEED_TRACKER_L1_H_
#define _SEEED_TRACKER_L1_H_
#include "WVariant.h"
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  Clock Configuration
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
#define VARIANT_MCK (64000000ul) // Master clock frequency
#define USE_LFXO                 // 32.768kHz crystal for LFCLK

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  Pin Capacity Definitions
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
#define PINS_COUNT (33u)       // Total GPIO pins
#define NUM_DIGITAL_PINS (33u) // Digital I/O pins
#define NUM_ANALOG_INPUTS (8u) // Analog inputs (A0-A5 + VBAT + AREF)
#define NUM_ANALOG_OUTPUTS (0u)

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  LED Configuration
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  LEDs
//  LEDs
#define PIN_LED1 (11) // LED        P1.15
#define PIN_LED2 (12) //

#define LED_BUILTIN PIN_LED1
#define LED_CONN PIN_LED2

#define LED_GREEN PIN_LED1
#define LED_BLUE PIN_LED2
// #define LED_PIN PIN_LED2
#define LED_STATE_ON 1 // State when LED is litted
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  Button Configuration
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
#define BUTTON_PIN D13 // This is the Program Button
// #define BUTTON_NEED_PULLUP   1
#define BUTTON_ACTIVE_LOW true
#define BUTTON_ACTIVE_PULLUP false

#define BUTTON_PIN_TOUCH 13 // Touch button
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  Digital Pin Mapping (D0-D10)
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
#define D0 0   // P1.06 GNSS_WAKEUP/IO0
#define D1 1   // P0.07 LORA_DIO1
#define D2 2   // P1.07 LORA_RESET
#define D3 3   // P1.10 LORA_BUSY
#define D4 4   // P1.14 LORA_CS
#define D5 5   // P1.08 LORA_SW
#define D6 6   // P0.27 GNSS_TX
#define D7 7   // P0.26 GNSS_RX
#define D8 8   // P0.30 SPI_SCK
#define D9 9   // P0.03 SPI_MISO
#define D10 10 // P0.28 SPI_MOSI
#define D12 12 // P1.00 Buzzer
#define D13 13 // P0.08 User Button
#define D14 14 // P0.05 OLED SCL
#define D15 15 // P0.06 OLED SDA
#define D16 16 // P0.31 VBAT_ADC
#define D17 17 // P0.00 GROVE SDA
#define D18 18 // P0.01 GROVE_SCL
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  Analog Pin Definitions
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
#define PIN_A0 0     // P0.02 Analog Input 0
#define PIN_A1 1     // P0.03 Analog Input 1
#define PIN_A2 2     // P0.28 Analog Input 2
#define PIN_A3 3     // P0.29 Analog Input 3
#define PIN_A4 4     // P0.04 Analog Input 4
#define PIN_A5 5     // P0.05 Analog Input 5
#define PIN_VBAT D16 // P0.31 Battery voltage sense
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  Communication Interfaces
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  I2C Configuration
#define HAS_WIRE 1
#define PIN_WIRE_SDA D14 // P0.09
#define PIN_WIRE_SCL D15 // P0.10
#define WIRE_INTERFACES_COUNT 1
#define I2C_NO_RESCAN

static const uint8_t SDA = PIN_WIRE_SDA;
static const uint8_t SCL = PIN_WIRE_SCL;

#define HAS_SCREEN 1
#define USE_SSD1306 1

// SPI Configuration (SX1262)

#define SPI_INTERFACES_COUNT 1
#define PIN_SPI_MISO 9  // P0.03 (D9)
#define PIN_SPI_MOSI 10 // P0.28 (D10)
#define PIN_SPI_SCK 8   // P0.30 (D8)

// SX1262 LoRa Module Pins
#define USE_SX1262
#define SX126X_CS D4                 // Chip select
#define SX126X_DIO1 D1               // Digital IO 1 (Interrupt)
#define SX126X_BUSY D3               // Busy status
#define SX126X_RESET D2              // Reset control
#define SX126X_DIO3_TCXO_VOLTAGE 1.8 // TCXO supply voltage
#define SX126X_RXEN D5               // RX enable control
#define SX126X_TXEN RADIOLIB_NC
#define SX126X_DIO2_AS_RF_SWITCH // This Line is really necessary for SX1262  to work with RF switch or will loss TX power
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  Power Management
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

#define BAT_READ 30 // D30 = P0.04  Reads battery voltage from divider on signal board.
#define BATTERY_SENSE_RESOLUTION_BITS 12
#define ADC_MULTIPLIER 2.0
#define BATTERY_PIN PIN_VBAT // PIN_A7
#define AREF_VOLTAGE 3.6
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  GPS L76KB
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
#define GPS_L76K
#ifdef GPS_L76K
#define PIN_GPS_RX D6 // P0.26
#define PIN_GPS_TX D7
#define HAS_GPS 1
#define GPS_BAUDRATE 9600
#define GPS_THREAD_INTERVAL 50
#define PIN_SERIAL1_RX PIN_GPS_TX
#define PIN_SERIAL1_TX PIN_GPS_RX

#define GPS_RX_PIN PIN_GPS_TX
#define GPS_TX_PIN PIN_GPS_RX
#define PIN_GPS_STANDBY D0

// #define GPS_DEBUG
//  #define GPS_EN D18 // P1.05
#endif

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  On-board QSPI Flash
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// On-board QSPI Flash
#define PIN_QSPI_SCK (21)
#define PIN_QSPI_CS (22)
#define PIN_QSPI_IO0 (23)
#define PIN_QSPI_IO1 (24)
#define PIN_QSPI_IO2 (25)
#define PIN_QSPI_IO3 (26)

#define EXTERNAL_FLASH_DEVICES P25Q16H
#define EXTERNAL_FLASH_USE_QSPI

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  Buzzer
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// Buzzer

#define PIN_BUZZER D12 // P1.00, pwm output

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  joystick
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

#define CANNED_MESSAGE_MODULE_ENABLE 1

// trackball
#define HAS_TRACKBALL 1
#define TB_UP 25
#define TB_DOWN 26
#define TB_LEFT 27
#define TB_RIGHT 28
#define TB_PRESS 29
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  Compatibility Definitions
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
#ifdef __cplusplus
extern "C" {
#endif
// Serial port placeholders

#define PIN_SERIAL2_RX (-1)
#define PIN_SERIAL2_TX (-1)
#ifdef __cplusplus
}
#endif

#endif //  _SEEED_SOLAR_NODE_H_