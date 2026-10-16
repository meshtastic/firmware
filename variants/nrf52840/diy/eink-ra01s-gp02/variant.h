// =============================================================================
// eink-ra01s-gp02  nRF52840 Board — Hardware Pin Definitions
// =============================================================================
// Hardware:
//   MCU     : nRF52840 QFN48 (Normal Voltage mode: VDD=VDDH=3.3V, LDO only)
//   LoRa    : Ai-Thinker RA-01S (SX1262/SX1268, 868/915 MHz)
//   GPS     : Ai-Thinker GP-02  (L76K compatible)
//   E-Ink   : GDEM0213B74 (SSD1680, 2.13", 250×122)
//   Buttons : 6-way F1..F6  + 3 LEDs (R/G/B)
//   RTC     : PCF8563 on I2C
//   Flash   : PY25Q32 on dedicated SPI
//   Battery : ADC on P0.04 (AIN2), power-ctrl P0.15 (active-high)
//   BT      : S140 v6.1.1 SoftDevice (app @ 0x26000)
// =============================================================================
//
// This file contains ONLY hardware pin definitions and required framework
// macros.  Software configuration (module settings, feature exclusions,
// calibration constants, UI setup) lives in config/*.h — see config/config.h.
// =============================================================================

#ifndef _VARIANT_EINK_RA01S_GP02_NRF52840_
#define _VARIANT_EINK_RA01S_GP02_NRF52840_

// ---------------------------------------------------------------------------
// Master clock
// ---------------------------------------------------------------------------
#define VARIANT_MCK (64000000ul)

// ---------------------------------------------------------------------------
// Low-frequency clock source — board has 32.768 kHz external crystal
// ---------------------------------------------------------------------------
#define USE_LFXO

// ---------------------------------------------------------------------------
// WVariant.h (Adafruit nRF52 core)
// ---------------------------------------------------------------------------
#include "WVariant.h"

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Pin count — nRF52840 has P0 (0–31) + P1 (32–47)
// ---------------------------------------------------------------------------
#define PINS_COUNT (48)
#define NUM_DIGITAL_PINS (48)
#define NUM_ANALOG_INPUTS (6)
#define NUM_ANALOG_OUTPUTS (0)

// ===========================================================================
// Feature presence flags
// ===========================================================================
#define HAS_RADIO 1  // LoRa RA-01S (SX1262)
#define HAS_GPS 1    // GPS GP-02 (L76K)
#define HAS_SCREEN 1 // E-Ink GDEM0213B74 (SSD1680)
#define HAS_BUTTON 1 // 6-way F1..F6

// ===========================================================================
// LEDs  (GPIO_ACTIVE_LOW, LED_STATE_ON = 0)
// ===========================================================================
#define PIN_LED_0 (32 + 3) // P1.03
#define PIN_LED_1 (32 + 1) // P1.01
#define PIN_LED_2 (0 + 14) // P0.14

#define LED_STATE_ON (LOW)

// ===========================================================================
// Buttons  (GPIO_PULL_UP | GPIO_ACTIVE_LOW)
// ===========================================================================
#define PIN_BTN_F1 (32 + 11) // P1.11
#define PIN_BTN_F2 (0 + 9)   // P0.09  [NFC1]
#define PIN_BTN_F3 (0 + 6)   // P0.06
#define PIN_BTN_F4 (0 + 8)   // P0.08
#define PIN_BTN_F5 (0 + 10)  // P0.10  [NFC2]
#define PIN_BTN_F6 (0 + 11)  // P0.11

// ===========================================================================
// LoRa — Ai-Thinker RA-01S (SX1262)
// SPI bus: SPI0 (SPIM0 / default SPI)
// ===========================================================================
#define PIN_LORA_PWR (0 + 13)  // P0.13  LDO enable (active-high)
#define PIN_LORA_SCK (0 + 19)  // P0.19
#define PIN_LORA_MOSI (0 + 22) // P0.22
#define PIN_LORA_MISO (0 + 23) // P0.23
#define PIN_LORA_CS (0 + 24)   // P0.24  chip select (active-low)
#define PIN_LORA_RST (0 + 25)  // P0.25  reset (active-low)
#define PIN_LORA_BUSY (0 + 17) // P0.17  busy output (active-high)
#define PIN_LORA_DIO1 (0 + 20) // P0.20  TX/RX done IRQ (active-high)

// ---------------------------------------------------------------------------
// Adafruit core SPI0 (default SPI) — LoRa
// ---------------------------------------------------------------------------
#define PIN_SPI_MISO PIN_LORA_MISO
#define PIN_SPI_MOSI PIN_LORA_MOSI
#define PIN_SPI_SCK PIN_LORA_SCK

static const uint8_t SS = PIN_LORA_CS;
static const uint8_t MOSI = PIN_LORA_MOSI;
static const uint8_t MISO = PIN_LORA_MISO;
static const uint8_t SCK = PIN_LORA_SCK;

// ===========================================================================
// GPS — Ai-Thinker GP-02 (L76K compatible, NMEA 9600 bps)
// UART: Serial1 (UART1, P1.08/P1.09)
// ===========================================================================
#define PIN_GNSS_TX (32 + 8)     // P1.08  nRF UART TX → GP-02 RX
#define PIN_GNSS_RX (32 + 9)     // P1.09  nRF UART RX ← GP-02 TX
#define PIN_GNSS_RST (32 + 5)    // P1.05  hard reset (active-high via NPN)
#define PIN_GNSS_PWR (0 + 12)    // P0.12  LDO enable (active-high)
#define PIN_GNSS_STBY (32 + 7)   // P1.07  standby (active-low; HIGH=normal)
#define PIN_GNSS_PPS (32 + 4)    // P1.04  pulse-per-second output
#define PIN_GNSS_REGION (32 + 2) // P1.02  constellation selection

#define GNSS_PWR_ACTIVE (HIGH)
#define GNSS_STBY_ACTIVE (LOW)

// ---------------------------------------------------------------------------
// Adafruit core Serial1 (UART1) — GPS
// ---------------------------------------------------------------------------
#define PIN_SERIAL1_RX PIN_GNSS_RX
#define PIN_SERIAL1_TX PIN_GNSS_TX

// ===========================================================================
// E-Ink — GDEM0213B74 (SSD1680, 2.13", 250×122)
// SPI bus: SPI1 (SPIM2, dedicated write-only)
//
// PIN_EINK_POWER controls the panel power LDO.  EINK_POWER_ACTIVE defines
// the active level (LOW = panel ON, HIGH = panel OFF for PCB v2).
// Note: PIN_EINK_EN is deliberately NOT defined — this board uses the
// PIN_EINK_POWER / EINK_POWER_ACTIVE pair instead.
// ===========================================================================
#define PIN_EINK_POWER (32 + 10) // P1.10  panel LDO enable
#define PIN_EINK_SCLK (0 + 31)   // P0.31
#define PIN_EINK_MOSI (0 + 29)   // P0.29
#define PIN_EINK_CS (0 + 30)     // P0.30  chip select (active-low)
#define PIN_EINK_DC (0 + 28)     // P0.28  data/command (HIGH=data, LOW=cmd)
#define PIN_EINK_RES (0 + 2)     // P0.2   reset (active-low, idle HIGH)
#define PIN_EINK_BUSY (0 + 3)    // P0.3   busy output (active-high)

#define EINK_POWER_ACTIVE (LOW) // LOW = panel power ON

// ---------------------------------------------------------------------------
// Adafruit core SPI1 — E-Ink (write-only, MISO unused)
// ---------------------------------------------------------------------------
#define PIN_SPI1_MISO (-1)
#define PIN_SPI1_MOSI PIN_EINK_MOSI
#define PIN_SPI1_SCK PIN_EINK_SCLK

// ===========================================================================
// External Flash — PY25Q32 (planned)
// SPI: dedicated instance, shares no pins with SPI0/SPI1
// ===========================================================================
#define PIN_EXT_FLASH_SCK (32 + 14)  // P1.14
#define PIN_EXT_FLASH_MOSI (32 + 12) // P1.12
#define PIN_EXT_FLASH_MISO (32 + 13) // P1.13
#define PIN_EXT_FLASH_CS (32 + 15)   // P1.15  chip select (active-low)
#define PIN_EXT_FLASH_WP (0 + 7)     // P0.7   write protect
#define PIN_EXT_FLASH_HOLD (0 + 5)   // P0.5   hold

// ===========================================================================
// I2C bus — PCF8563 RTC (and future sensors)
// P0.26 = SDA, P0.27 = SCL, external pull-ups on PCB
// ===========================================================================
#define WIRE_INTERFACES_COUNT 1
#define PIN_WIRE_SDA (0 + 26) // P0.26
#define PIN_WIRE_SCL (0 + 27) // P0.27

// ===========================================================================
// SPI bus count
//   0 = SPI0  (SPIM0) — LoRa
//   1 = SPI1  (SPIM2) — E-Ink
// ===========================================================================
#define SPI_INTERFACES_COUNT 2

// ===========================================================================
// Battery ADC  — P0.04 (AIN2), 50:50 divider (×2)
// Power control — P0.15, active-high (HIGH = divider enabled)
// ===========================================================================
#define PIN_BATTERY_ADC (0 + 4)  // P0.04 = AIN2
#define PIN_BATTERY_PWR (0 + 15) // P0.15  divider enable (active-high)

// ===========================================================================
// External UART listen (RX-only) — P1.00 @ 1200 bps
// ===========================================================================
#define PIN_EXT_UART_RX (32 + 0) // P1.00

// ===========================================================================
// Serial port override — nRF52840 uses Serial (USB-CDC) for logging
// ===========================================================================
#define SERIAL_PRINT_PORT 0

// ---------------------------------------------------------------------------
// RTC CLKOUT pin (not used by firmware)
// ---------------------------------------------------------------------------
#define PIN_RTC_CLKOUT (0 + 16) // P0.16

#ifdef __cplusplus
}
#endif

// ===========================================================================
// Software configuration (module settings, feature exclusions, calibration…)
// ===========================================================================
#include "config/config.h"

#endif /* _VARIANT_EINK_RA01S_GP02_NRF52840_ */
