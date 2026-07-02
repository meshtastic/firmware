#ifndef _T_IMPULSE_PLUS_H_
#define _T_IMPULSE_PLUS_H_
#include "WVariant.h"

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  Clock Configuration
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
#define VARIANT_MCK (64000000ul)
#define USE_LFXO

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  Pin Capacity Definitions
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
#define PINS_COUNT (30u)
#define NUM_DIGITAL_PINS (30u)
#define NUM_ANALOG_INPUTS (1u)
#define NUM_ANALOG_OUTPUTS (0u)

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  Digital Pin Mapping
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
#define D0 0   // P0.02  SX1262_RST
#define D1 1   // P0.29  SX1262_DIO1
#define D2 2   // P0.31  SX1262_BUSY
#define D3 3   // P1.14  SX1262_CS
#define D4 4   // P0.03  SPI_SCK
#define D5 5   // P0.30  SPI_MISO
#define D6 6   // P0.28  SPI_MOSI
#define D7 7   // P1.13  RF_VC1 (TXEN)
#define D8 8   // P1.07  RF_VC2 (RXEN)
#define D9 9   // P1.12  GPS module TX → MCU RX
#define D10 10 // P1.11  GPS module RX ← MCU TX
#define D11 11 // P1.10  GPS_EN (active LOW)
#define D12 12 // P0.20  SCREEN_SDA
#define D13 13 // P0.15  SCREEN_SCL
#define D14 14 // P1.08  IMU_SDA
#define D15 15 // P0.11  IMU_SCL
#define D16 16 // P0.05  BATTERY_ADC
#define D17 17 // P0.25  BATTERY_CTL
#define D18 18 // P1.04  TTP223_KEY
#define D19 19 // P0.22  VIBRATION_MOTOR
#define D20 20 // P0.14  RT9080_EN
#define D21 21 // P0.12  FLASH_CS
#define D22 22 // P0.04  FLASH_SCLK
#define D23 23 // P0.06  FLASH_IO0
#define D24 24 // P1.09  FLASH_IO1
#define D25 25 // P0.08  FLASH_IO2
#define D26 26 // P0.26  FLASH_IO3
#define D27 27 // P0.07  ICM20948_INT
#define D28 28 // P0.16  SGM41562_INT
#define D29 29 // P0.24  BOOT

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  LED Configuration (no physical LED)
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
#define LED_STATE_ON 1

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  Button Configuration (TTP223 capacitive touch)
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
#define PIN_BUTTON_TOUCH D18
#define BUTTON_TOUCH_ACTIVE_LOW true
#define BUTTON_TOUCH_ACTIVE_PULLUP false

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  Analog Pin Definitions
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
#define PIN_VBAT D16 // P0.05 Battery voltage sense

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  I2C Configuration
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// Primary I2C: Display (SSD1315)
#define PIN_WIRE_SDA D12 // P0.20
#define PIN_WIRE_SCL D13 // P0.15

// Secondary I2C: IMU (ICM20948) + PMU (SGM41562)
#define WIRE_INTERFACES_COUNT 2
#define PIN_WIRE1_SDA D14 // P1.08
#define PIN_WIRE1_SCL D15 // P0.11
#define I2C_NO_RESCAN

static const uint8_t SDA = PIN_WIRE_SDA;
static const uint8_t SCL = PIN_WIRE_SCL;

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  Display (SSD1315, compatible with SSD1306 driver)
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
#define HAS_SCREEN 1
#define USE_SSD1306 1
#define OLED_TINY
#define OLED_GEOMETRY_OVERRIDE GEOMETRY_64_32

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  SPI Configuration (SX1262 LoRa)
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
#define SPI_INTERFACES_COUNT 1
#define PIN_SPI_SCK D4  // P0.03
#define PIN_SPI_MISO D5 // P0.30
#define PIN_SPI_MOSI D6 // P0.28

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  SX1262 LoRa (S62F module)
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
#define USE_SX1262
#define SX126X_CS D3
#define SX126X_DIO1 D1
#define SX126X_BUSY D2
#define SX126X_RESET D0
#define SX126X_DIO3_TCXO_VOLTAGE 1.8
#define SX126X_TXEN D7 // RF_VC1
#define SX126X_RXEN D8 // RF_VC2

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  Power Management
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
#define BAT_READ D17 // P0.25 Battery measurement control (HIGH = enable)
#define BATTERY_PIN PIN_VBAT
#define BATTERY_SENSE_RESOLUTION_BITS 12
#define ADC_MULTIPLIER 2.0
#define AREF_VOLTAGE 3.6

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  GPS (u-blox MIA-M10Q)
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
#define GPS_UBLOX
#define HAS_GPS 1
#define GPS_RX_PIN D9  // P1.12 - MCU RX, wired to GPS module TX
#define GPS_TX_PIN D10 // P1.11 - MCU TX, wired to GPS module RX
#define PIN_GPS_EN D11 // P1.10
#define GPS_EN_ACTIVE LOW
#define GPS_BAUDRATE 38400
#define GPS_THREAD_INTERVAL 50
#define PIN_SERIAL1_TX GPS_TX_PIN
#define PIN_SERIAL1_RX GPS_RX_PIN

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  On-board QSPI Flash (ZD25WQ32CEIGR)
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
#define PIN_QSPI_SCK D22 // P0.04
#define PIN_QSPI_CS D21  // P0.12
#define PIN_QSPI_IO0 D23 // P0.06
#define PIN_QSPI_IO1 D24 // P1.09
#define PIN_QSPI_IO2 D25 // P0.08
#define PIN_QSPI_IO3 D26 // P0.26

#define EXTERNAL_FLASH_DEVICES W25Q32JV_IQ
#define EXTERNAL_FLASH_USE_QSPI

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  Vibration Motor (GPIO active-high)
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
#define LED_NOTIFICATION D19 // P0.22
#define HAPTIC_FEEDBACK_PIN LED_NOTIFICATION

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  IMU (ICM20948 on Wire1)
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
#define HAS_ICM20948

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  Charger (SGM41562 on Wire1 @ 0x03)
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
#define HAS_SGM41562
#define SGM41562_WIRE Wire1

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  Compatibility Definitions
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
#ifdef __cplusplus
extern "C" {
#endif

#define PIN_SERIAL2_RX (-1)
#define PIN_SERIAL2_TX (-1)

#ifdef __cplusplus
}
#endif

#endif // _T_IMPULSE_PLUS_H_