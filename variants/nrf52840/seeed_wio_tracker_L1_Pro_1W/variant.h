#ifndef _SEEED_TRACKER_L1_PRO_1W_H_
#define _SEEED_TRACKER_L1_PRO_1W_H_

#include "WVariant.h"

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  Clock Configuration
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
#define VARIANT_MCK (64000000ul) // Master clock frequency
#define USE_LFXO                 // 32.768kHz crystal for LFCLK

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  Pin Capacity Definitions
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
#define PINS_COUNT (34u)       // Total GPIO pins (D0-D33)
#define NUM_DIGITAL_PINS (34u) // Digital I/O pins
#define NUM_ANALOG_INPUTS (8u) // Analog inputs (A0-A5 + VBAT + AREF)
#define NUM_ANALOG_OUTPUTS (0u)

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  LED Configuration
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  Only one real LED (orange, P1.01). PIN_LED2/LED_BLUE/LED_CONN alias D12 (buzzer) for
//  ABI compatibility with app code; they drive no hardware LED.
#define PIN_LED1 (11) // Mesh_LED orange  P1.01
#define PIN_LED2 (12) // buzzer pin (no real LED on L1 Pro 1W)

#define LED_GREEN PIN_LED1
#define LED_BLUE PIN_LED2
#define LED_STATE_ON 1 // State when LED is lit

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  Button Configuration
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
#define CANCEL_BUTTON_PIN D13 // Program Button
#define CANCEL_BUTTON_ACTIVE_LOW true
#define CANCEL_BUTTON_ACTIVE_PULLUP false

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  Digital Pin Mapping (D0-D32)
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  D5 / D11 / D31 / D32 are the Pro 1W V1.0 hardware-revision pins
#define D0 0   // P1.09 GNSS_WAKEUP/IO0
#define D1 1   // P0.07 LORA_DIO1
#define D2 2   // P1.07 LORA_RESET
#define D3 3   // P1.10 LORA_BUSY
#define D4 4   // P1.14 LORA_CS
#define D5 5   // P0.29 LORA_VDET (AIN5), replaces the stock L1 LORA_SW on P1.08
#define D6 6   // P0.27 GNSS_TX
#define D7 7   // P0.26 GNSS_RX
#define D8 8   // P0.30 SPI_SCK
#define D9 9   // P0.03 SPI_MISO
#define D10 10 // P0.28 SPI_MOSI
#define D11 11 // P1.01 Mesh_LED (orange)
#define D12 12 // P1.00 Buzzer
#define D13 13 // P0.08 User Button
#define D14 14 // P0.06 OLED SDA
#define D15 15 // P0.05 OLED SCL
#define D16 16 // P0.31 VBAT_ADC
#define D17 17 // P1.11 Grove I2C1 SCL
#define D18 18 // P1.12 Grove I2C1 SDA
#define D31 31 // P0.13 BOOST_EN (Grove 5V Boost enable), new on Pro 1W
#define D32 32 // P1.15 nRF_Sig_Charge_State (BQ25616 STAT), new on Pro 1W
#define D33 33 // P0.14 LORA_PWR_EN (SX1262 + 1 W PA LDO), new on Pro 1W

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
#define PIN_WIRE_SDA D14 // P0.06 OLED SDA
#define PIN_WIRE_SCL D15 // P0.05 OLED SCL
#define WIRE_INTERFACES_COUNT 2
#define PIN_WIRE1_SDA D18
#define PIN_WIRE1_SCL D17
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
#define SX126X_RXEN RADIOLIB_NC
#define SX126X_TXEN RADIOLIB_NC
#define SX126X_DIO2_AS_RF_SWITCH // DIO2 controls antenna switch (no external RXEN/TXEN)

// SX1262 drives a 1 W external PA; use the fixed PA config, not RadioLib's table.
#define SX126X_NO_POWER_OPTIMIZATION_TABLE

// Chip-side drive ceiling; limitPower() already subtracted the PA gain. TODO: verify on bench.
#define SX126X_MAX_POWER 22

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  Power Management
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
#define BAT_READ 30 // D30 = P0.04  Battery divider enable (BAT_CTL) on signal board.
#define ADC_CTRL BAT_READ
#define ADC_CTRL_ENABLED HIGH
#define BATTERY_SENSE_RESOLUTION_BITS 12
#define ADC_MULTIPLIER 2.0
#define BATTERY_PIN PIN_VBAT
#define AREF_VOLTAGE 3.6
// We rely on the nrf52840 USB controller to tell us if we are hooked to a power supply
#define NRF_APM

// BQ25616 single-wire charge status (Pro 1W)
#define PIN_BOOST_EN D31          // D31 / P0.13, Grove 5V Boost enable
#define EXT_CHRG_DETECT D32       // D32 / P1.15, BQ25616 STAT
#define EXT_CHRG_DETECT_VALUE LOW // 0 = charging, 1 = full / charger sleep
#define BOOST_EN_ACTIVE HIGH      // HIGH enables Grove 5V Boost

// External LDO enable for the SX1262 + 1 W PA. D33 rather than raw GPIO 14 because
// g_ADigitalPinMap[14] is D14 (OLED SDA). init() drives it HIGH; deep sleep does not clear it.
#define LORA_PWR_EN D33
#define SX126X_POWER_EN LORA_PWR_EN

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  GPS L76KB
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
#define GPS_L76K
#ifdef GPS_L76K
#define GPS_TX_PIN D6 // P0.26 - This is data from the MCU
#define GPS_RX_PIN D7 // P0.27 - This is data from the GNSS
#define HAS_GPS 1
#define GPS_BAUDRATE 9600
#define GPS_THREAD_INTERVAL 50
#define PIN_SERIAL1_RX GPS_RX_PIN
#define PIN_SERIAL1_TX GPS_TX_PIN

#define PIN_GPS_STANDBY D0
#endif

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  On-board QSPI Flash
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// Logical pin indices; the QSPI block is at D19-D24 in variant.cpp.
#define PIN_QSPI_SCK (19)
#define PIN_QSPI_CS (20)
#define PIN_QSPI_IO0 (21)
#define PIN_QSPI_IO1 (22)
#define PIN_QSPI_IO2 (23)
#define PIN_QSPI_IO3 (24)

#define EXTERNAL_FLASH_DEVICES P25Q16H
#define EXTERNAL_FLASH_USE_QSPI

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  Buzzer
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
#define PIN_BUZZER D12 // P1.00, pwm output

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  Trackball
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
#define CANNED_MESSAGE_ADD_CONFIRMATION 1

#define HAS_TRACKBALL 1
#define TB_UP 25
#define TB_DOWN 26
#define TB_LEFT 27
#define TB_RIGHT 28
#define TB_PRESS 29
#define TB_DIRECTION FALLING

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

#endif //  _SEEED_TRACKER_L1_PRO_1W_H_
