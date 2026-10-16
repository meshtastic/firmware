/*
 Copyright (c) 2014-2015 Arduino LLC.  All right reserved.
 Copyright (c) 2016 Sandeep Mistry All right reserved.
 Copyright (c) 2018, Adafruit Industries (adafruit.com)

 This library is free software; you can redistribute it and/or
 modify it under the terms of the GNU Lesser General Public
 License as published by the Free Software Foundation; either
 version 2.1 of the License, or (at your option) any later version.
 This library is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 See the GNU Lesser General Public License for more details.
 You should have received a copy of the GNU Lesser General Public
 License along with this library; if not, write to the Free Software
 Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

#ifndef _VARIANT_NODARA_NRF52840_
#define _VARIANT_NODARA_NRF52840_

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
#endif /* __cplusplus */

// ---------------------------------------------------------------------------
// Pin count — nRF52840 has P0 (0–31) + P1 (32–47)
// ---------------------------------------------------------------------------
#define PINS_COUNT (48)
#define NUM_DIGITAL_PINS (48)
#define NUM_ANALOG_INPUTS (1)
#define NUM_ANALOG_OUTPUTS (0)

// ===========================================================================
// Feature presence flags
// ===========================================================================
#define HAS_RADIO       1           // LoRa RA-01S (SX1262)
#define HAS_GPS         1           // GPS GP-02 (L76K)
#define HAS_SCREEN      1           // E-Ink GDEM0213B74 (SSD1680)
#define HAS_BUTTON      1           // 6-way F1..F6
#define HAS_RTC         1           // IIC

// ===========================================================================
// LEDs  (GPIO_ACTIVE_LOW, LED_STATE_ON = 0)
// ===========================================================================
//#define PIN_LED1        (32 + 3)    // P1.03 red
#define PIN_LED2        (14 + 0)    // P0.14 green
#define PIN_LED3        (32 + 1)    // P1.01 blue

#define LED_STATE_ON    (LOW)

// ===========================================================================
// Buttons  (GPIO_PULL_UP | GPIO_ACTIVE_LOW)
// ===========================================================================
#define PIN_BTN_F1      (32 + 11)  // P1.11
#define PIN_BTN_F2      (0 + 9)    // P0.09  [NFC1]
#define PIN_BTN_F3      (0 + 6)    // P0.06
#define PIN_BTN_F4      (0 + 8)    // P0.08
#define PIN_BTN_F5      (0 + 10)   // P0.10  [NFC2]
#define PIN_BTN_F6      (0 + 11)   // P0.11

#define PIN_BTN_ACTIVE  (LOW)

// ===========================================================================
// External UART listen (RX-only) — P1.00 @ 1200 bps
// ===========================================================================
#define MCU_DATA_PIN        (32 + 0) // P1.00

// ---------------------------------------------------------------------------
// Adafruit core Serial2 (UART2) — MCU
// ---------------------------------------------------------------------------
#define PIN_SERIAL2_RX MCU_DATA_PIN
#define PIN_SERIAL2_TX (-1)

// ===========================================================================
// Battery ADC  — P0.04 (AIN2), 50:50 divider (×2)
// Power control — P0.15, active-high (HIGH = divider enabled)
// ===========================================================================
#define PIN_BATTERY_ADC     (0 + 4)    // P0.04 = AIN2
#define PIN_BATTERY_PWR     (0 + 15)   // P0.15  divider enable (active-high)

static const uint8_t A0 = PIN_BATTERY_ADC;

// ===========================================================================
// I2C bus — PCF8563 RTC (and future sensors)
// P0.26 = SDA, P0.27 = SCL, external pull-ups on PCB
// ===========================================================================
#define WIRE_INTERFACES_COUNT   1
#define PIN_WIRE_SDA            (0 + 26)  // P0.26
#define PIN_WIRE_SCL            (0 + 27)  // P0.27
#define PIN_RTC_INT             (0 + 16)  // Interrupt from the PCF8563 RTC

// ===========================================================================
// External Flash — PY25Q32 (planned)
// SPI: dedicated instance, shares no pins with SPI0/SPI1
// ===========================================================================
#define PIN_EXT_FLASH_SCK   (32 + 14)  // P1.14
#define PIN_EXT_FLASH_MOSI  (32 + 12)  // P1.12
#define PIN_EXT_FLASH_MISO  (32 + 13)  // P1.13
#define PIN_EXT_FLASH_CS    (32 + 15)  // P1.15  chip select (active-low)
#define PIN_EXT_FLASH_WP    (0 + 7)    // P0.7   write protect
#define PIN_EXT_FLASH_HOLD  (0 + 5)    // P0.5   hold

// QSPI Pins
#define PIN_QSPI_SCK (32 + 14)
#define PIN_QSPI_CS (32 + 15)
#define PIN_QSPI_IO0 (32 + 12) // MOSI if using two bit interface
#define PIN_QSPI_IO1 (32 + 13) // MISO if using two bit interface
#define PIN_QSPI_IO2 (0 + 7)   // WP if using two bit interface (i.e. not used)
#define PIN_QSPI_IO3 (0 + 5)   // HOLD if using two bit interface (i.e. not used)

// .pio/libdeps/nodara/SdFat - Adafruit Fork/src/SdFat.h:57:30: error:
#ifndef SS
#define SS PIN_QSPI_CS
#endif

// On-board QSPI Flash
#define EXTERNAL_FLASH_DEVICES P25Q32
#define EXTERNAL_FLASH_USE_QSPI     // Disabled for power comparison test

// ===========================================================================
// LoRa — Ai-Thinker RA-01S (SX1262)
// SPI bus: SPI0 (SPIM0 / default SPI)
// ===========================================================================
#define PIN_LORA_PWR    (0 + 13)   // P0.13  LDO enable (active-high)
#define PIN_LORA_SCK    (0 + 19)   // P0.19
#define PIN_LORA_MOSI   (0 + 22)   // P0.22
#define PIN_LORA_MISO   (0 + 23)   // P0.23
#define PIN_LORA_CS     (0 + 24)   // P0.24  chip select (active-low)
#define PIN_LORA_RST    (0 + 25)   // P0.25  reset (active-low)
#define PIN_LORA_BUSY   (0 + 17)   // P0.17  busy output (active-high)
#define PIN_LORA_DIO1   (0 + 20)   // P0.20  TX/RX done IRQ (active-high)
#define PIN_LORA_DIO3   (0 + 21)

// ---------------------------------------------------------------------------
// Adafruit core SPI0 (default SPI) — LoRa
// ---------------------------------------------------------------------------
#define PIN_SPI_MISO    PIN_LORA_MISO
#define PIN_SPI_MOSI    PIN_LORA_MOSI
#define PIN_SPI_SCK     PIN_LORA_SCK

// static const uint8_t SS   = PIN_LORA_CS;
// static const uint8_t MOSI = PIN_LORA_MOSI;
// static const uint8_t MISO = PIN_LORA_MISO;
// static const uint8_t SCK  = PIN_LORA_SCK;

// ===========================================================================
// E-Ink — GDEM0213B74 (SSD1680, 2.13", 250×122)
// SPI bus: SPI1 (SPIM2, dedicated write-only)
//
// PIN_EINK_POWER controls the panel power LDO.  EINK_POWER_ACTIVE defines
// the active level (LOW = panel ON, HIGH = panel OFF for PCB v2).
// Note: PIN_EINK_EN is deliberately NOT defined — this board uses the
// PIN_EINK_POWER / EINK_POWER_ACTIVE pair instead.
// ===========================================================================
#define PIN_EINK_POWER  (32 + 10)  // P1.10  panel LDO enable
#define PIN_EINK_SCLK   (0 + 31)   // P0.31
#define PIN_EINK_MOSI   (0 + 29)   // P0.29
#define PIN_EINK_CS     (0 + 30)   // P0.30  chip select (active-low)
#define PIN_EINK_DC     (0 + 28)   // P0.28  data/command (HIGH=data, LOW=cmd)
#define PIN_EINK_RES    (0 + 2)    // P0.2   reset (active-low, idle HIGH)
#define PIN_EINK_BUSY   (0 + 3)    // P0.3   busy output (active-high)

#define EINK_POWER_ACTIVE (LOW)    // LOW = panel power ON

// ---------------------------------------------------------------------------
// Adafruit core SPI1 — E-Ink (write-only, MISO unused)
// ---------------------------------------------------------------------------
#define PIN_SPI1_MISO   (-1)
#define PIN_SPI1_MOSI   PIN_EINK_MOSI
#define PIN_SPI1_SCK    PIN_EINK_SCLK

// ===========================================================================
// GPS — Ai-Thinker GP-02 (L76K compatible, NMEA 9600 bps)
// UART: Serial1 (UART1, P1.08/P1.09)
// ===========================================================================
#define PIN_GNSS_TX     (32 + 8)   // P1.08  nRF UART TX → GP-02 RX
#define PIN_GNSS_RX     (32 + 9)   // P1.09  nRF UART RX ← GP-02 TX
#define PIN_GNSS_RST    (32 + 5)   // P1.05  hard reset (active-high via NPN)
#define PIN_GNSS_PWR    (0 + 12)   // P0.12  LDO enable (active-high)
#define PIN_GNSS_STBY   (32 + 7)   // P1.07  standby (active-low; HIGH=normal)
#define PIN_GNSS_PPS    (32 + 4)   // P1.04  pulse-per-second output
#define PIN_GNSS_REGION (32 + 2)   // P1.02  constellation selection

#define GNSS_PWR_ACTIVE  (HIGH)
#define GNSS_STBY_ACTIVE (LOW)

// ---------------------------------------------------------------------------
// Adafruit core Serial1 (UART1) — GPS
// ---------------------------------------------------------------------------
#define PIN_SERIAL1_RX  PIN_GNSS_RX
#define PIN_SERIAL1_TX  PIN_GNSS_TX

// ===========================================================================
// SPI bus count
//   0 = SPI0  (SPIM0) — LoRa
//   1 = SPI1  (SPIM2) — E-Ink
//   2 = SPI3  (SPIM1) — Flash
// ===========================================================================
#define SPI_INTERFACES_COUNT    2

// ===========================================================================
// Serial port override — nRF52840 uses Serial (USB-CDC) for logging
// ===========================================================================
#define SERIAL_PRINT_PORT   0

// ===========================================================================
// Software configuration (module settings, feature exclusions, calibration…)
// ===========================================================================

// ---------------------------------------------------------------------------
// Enabled modules (explicitly not excluded)
// ---------------------------------------------------------------------------
#define MESHTASTIC_EXCLUDE_SIXKEYNAVINPUT   0   // 6-button polling driver
#define MESHTASTIC_EXCLUDE_I2C              0   // PCF8563 RTC on I2C
#define MESHTASTIC_EXCLUDE_POWER_FSM        0   // Required for BLE advertising

// ---------------------------------------------------------------------------
// NRF_APM: use nrfx_power_usbstatus_get() to detect USB VBUS state.
// Required for correct isPowered() / USB detection on nRF52840 without PMU.
// ---------------------------------------------------------------------------
#define NRF_APM

// ---------------------------------------------------------------------------
// USE_POWERSAVE: forces is_power_saving = true. Enable when bench-testing
// without a battery (ADC reads ~0 mV → false external-power detection).
// Battery IS connected on this board; keep commented.
// ---------------------------------------------------------------------------
// #define USE_POWERSAVE

// ---------------------------------------------------------------------------
// USE_SEGGER: redirect LOG_* output to SEGGER RTT (SWD/J-Link) instead of
// USB-CDC.  Enable when the device has no USB connected.
// ---------------------------------------------------------------------------
// #define USE_SEGGER

// ---------------------------------------------------------------------------
// BLE configuration
// ---------------------------------------------------------------------------
// #define USERPREFS_FIXED_BLUETOOTH   123456  // Fixed pairing PIN
// #define BLE_ADV_FAST_INTERVAL       800     // 800 × 0.625 ms = 500 ms
// #define BLE_ADV_SLOW_INTERVAL       1600    // 1600 × 0.625 ms = 1000 ms
// #define BLE_ADV_FAST_TIMEOUT_S      0       // Skip fast mode, use slow immediately

// ---------------------------------------------------------------------------
// Environment telemetry is display-only (USB-injected). No onboard I2C sensors.
// ---------------------------------------------------------------------------
// #define NODARA_USB_INJECTED_TELEMETRY 1
#define MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR        1
#define MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR_EXTERNAL 1
#define MESHTASTIC_EXCLUDE_POWER_TELEMETRY             1
#define MESHTASTIC_EXCLUDE_AIR_QUALITY_SENSOR          1
#define MESHTASTIC_EXCLUDE_HEALTH_TELEMETRY            1
#define MESHTASTIC_EXCLUDE_ACCELEROMETER               1
#define MESHTASTIC_EXCLUDE_DETECTIONSENSOR             1

// ---------------------------------------------------------------------------
// No audio
// ---------------------------------------------------------------------------
#define MESHTASTIC_EXCLUDE_AUDIO            1

// ---------------------------------------------------------------------------
// No BLE PAX counting
// ---------------------------------------------------------------------------
#define MESHTASTIC_EXCLUDE_PAXCOUNTER       1

// ---------------------------------------------------------------------------
// No peripheral notification module
// ---------------------------------------------------------------------------
#define MESHTASTIC_EXCLUDE_EXTERNALNOTIFICATION 1

// ---------------------------------------------------------------------------
// Unused features and network modules
// ---------------------------------------------------------------------------
#define MESHTASTIC_EXCLUDE_MQTT             1   // nRF52 has no WiFi
#define MESHTASTIC_EXCLUDE_SERIAL           1   // Serial bridge module
#define MESHTASTIC_EXCLUDE_STOREFORWARD     1   // RAM-heavy, not needed
#define MESHTASTIC_EXCLUDE_DROPZONE         1   // File sharing
#define MESHTASTIC_EXCLUDE_ATAK             1   // ATAK integration
#define MESHTASTIC_EXCLUDE_REPLYBOT         1   // Auto-reply bot
#define MESHTASTIC_EXCLUDE_REMOTEHARDWARE   1   // Remote GPIO control
#define MESHTASTIC_EXCLUDE_POWERSTRESS      1   // Stress-test tool

// ---------------------------------------------------------------------------
// Role assignments. No power LED: leave LED_POWER undefined.
// ---------------------------------------------------------------------------
#define LED_USB         PIN_LED2        // green: USB connected / charge complete
#define LED_PAIRING     PIN_LED3        // blue: BLE pairing / connected

// Legacy Bluefruit color aliases (do not match physical LED colors)
// #define LED_RED         PIN_LED1
#define LED_BLUE        PIN_LED3
#define LED_GREEN       PIN_LED2
#define LED_CONN        LED_PAIRING

// ---------------------------------------------------------------------------
// SixKeyNavInput expects these names
// ---------------------------------------------------------------------------
#define PIN_BUTTON_RIGHT   PIN_BTN_F3
#define PIN_BUTTON_DOWN    PIN_BTN_F4
#define PIN_BUTTON_LEFT    PIN_BTN_F2
#define PIN_BUTTON_A       PIN_BTN_F5
#define PIN_BUTTON_B       PIN_BTN_F6
#define PIN_BUTTON_UP      PIN_BTN_F1

// ---------------------------------------------------------------------------
// Radio module selection
// The RA-01S physically contains an SX1268 (China-band); its register map is
// identical to SX1262.  USE_SX1262 is correct — do NOT use USE_SX1268.
// ---------------------------------------------------------------------------
#define USE_SX1262

// ---------------------------------------------------------------------------
// Framework pin aliases (SX126xInterface constructor args)
// ---------------------------------------------------------------------------
#define SX126X_CS      PIN_LORA_CS
#define SX126X_DIO1    PIN_LORA_DIO1
#define SX126X_RESET   PIN_LORA_RST
#define SX126X_BUSY    PIN_LORA_BUSY
#define SX1262_DIO3    PIN_LORA_DIO3

// ---------------------------------------------------------------------------
// TCXO — NOT present on RA-01S (crystal oscillator variant).
// Do NOT define SX126X_DIO3_TCXO_VOLTAGE.
// (Uncomment only for RA-01SH which has a TCXO.)
// ---------------------------------------------------------------------------
// #define SX126X_DIO3_TCXO_VOLTAGE  1.8
// #define TCXO_OPTIONAL

// ---------------------------------------------------------------------------
// Power enable — RA-01S LDO on P0.13 (active-high)
// ---------------------------------------------------------------------------
#define SX126X_POWER_EN   PIN_LORA_PWR

// ---------------------------------------------------------------------------
// RF switch — RA-01S uses DIO2 internally for TX/RX antenna switching
// No external MCU pin required; RadioLib handles it via SetDio2AsRfSwitchCtrl.
// ---------------------------------------------------------------------------
#define SX126X_DIO2_AS_RF_SWITCH

// ---------------------------------------------------------------------------
// Maximum TX power (dBm)
// ---------------------------------------------------------------------------
#define SX126X_MAX_POWER  22

// ---------------------------------------------------------------------------
// Framework pin aliases
// ---------------------------------------------------------------------------
#define GPS_RX_PIN       PIN_GNSS_RX
#define GPS_TX_PIN       PIN_GNSS_TX
#define PIN_GPS_RESET    PIN_GNSS_RST
#define PIN_GPS_EN       PIN_GNSS_PWR
#define PIN_GPS_STANDBY  PIN_GNSS_STBY
#define PIN_GPS_PPS      PIN_GNSS_PPS

#define GPS_EN_ACTIVE      GNSS_PWR_ACTIVE
#define GPS_STANDBY_ACTIVE GNSS_STBY_ACTIVE

// ---------------------------------------------------------------------------
// Baud rate — GP-02 default is 9600 bps (matches Meshtastic framework default)
// Only define GPS_BAUDRATE if a non-default rate is needed.
// ---------------------------------------------------------------------------
// #define GPS_BAUDRATE  9600

// ---------------------------------------------------------------------------
// Panel model
// GxEPD2_213_B74: driver specifically for GDEM0213B74 (SSD1680).
// Uses the OTP built-in LUT for partial updates — correct for this panel.
// Physical: WIDTH=128 gate lines, HEIGHT=250 source lines.
// After setRotation(3): display coordinate width=250, height=128.
// ---------------------------------------------------------------------------
#define EINK_DISPLAY_MODEL  GxEPD2_213_B74
#define EINK_WIDTH          250   // display width  after rotation-3 (source lines)
#define EINK_HEIGHT         122   // display height after rotation-3 (visible gate lines)

// ---------------------------------------------------------------------------
// Screen rotation
//   0 → 250w × 122h  landscape, FPC at bottom
//   1 → 122w × 250h  portrait
//   2 → 250w × 122h  landscape, FPC at top (180°)
//   3 → 122w × 250h  portrait, rotated 180°
// ---------------------------------------------------------------------------
#define EINK_ROTATION   3

// ---------------------------------------------------------------------------
// Dynamic refresh control
// ---------------------------------------------------------------------------
#define EINK_LIMIT_FASTREFRESH      (20)
#define EINK_LIMIT_GHOSTING_PX      (EINK_WIDTH * EINK_HEIGHT / 2)
#define EINK_BACKGROUND_USES_FAST
#define EINK_HASQUIRK_GHOSTING

// ---------------------------------------------------------------------------
// ADC input pin (must match PIN_BATTERY_ADC in variant.h)
// ---------------------------------------------------------------------------
#define BATTERY_PIN                     PIN_BATTERY_ADC // P0.04 = AIN2 (Arduino pin 4)
#define ADC_MULTIPLIER                  2.0f            // 50:50 divider: V_batt = V_adc × 2
#define BATTERY_SENSE_RESOLUTION_BITS   12
#define BATTERY_SENSE_RESOLUTION        4096.0
#define VBAT_AR_INTERNAL                AR_INTERNAL_3_0

// ---------------------------------------------------------------------------
// Voltage-divider power-enable (active-high)
// P0.15 = Arduino pin 15.
// Power.cpp guards every analogRead() burst with adcEnable/adcDisable.
// ---------------------------------------------------------------------------
#define ADC_CTRL            PIN_BATTERY_PWR     // P0.15
#define ADC_CTRL_ENABLED    HIGH                // active-high

// ---------------------------------------------------------------------------
// ADC reference voltage
// Adafruit nRF52 BSP configures SAADC with internal 0.6 V reference
// and 1/6 gain → full-scale = 0.6 V / (1/6) = 3.6 V.
// Using 3.3 would cause a systematic ~9 % under-read.
// ---------------------------------------------------------------------------
#define AREF_VOLTAGE        3.0

// ---------------------------------------------------------------------------
// OCV (Open-Circuit Voltage) discharge curve — standard single-cell LiPo
// Values in mV, ordered 100 % → 0 % SOC (11 equally-spaced points).
// ---------------------------------------------------------------------------
#define NUM_OCV_POINTS  11
#define OCV_ARRAY       4190, 4050, 3990, 3890, 3800, 3720, 3630, 3530, 3420, 3300, 3100
#define NUM_CELLS       1

// ---------------------------------------------------------------------------
// PCF8563 I2C address
// Enables the PCF8563_RTC branch in ScanI2CTwoWire.cpp + RTC.cpp:
//   - readFromRTC(): reads datetime → RTCQualityDevice on boot
//   - perhapsSetRTC(): writes datetime back when GPS/NTP improves quality
// ---------------------------------------------------------------------------
#define PCF8563_RTC  0x51


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _VARIANT_NODARA_NRF52840_ */
