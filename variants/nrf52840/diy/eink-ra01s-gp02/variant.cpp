/*
  Copyright (c) 2014-2015 Arduino LLC.  All right reserved.
  Copyright (c) 2016 Sandeep Mistry All right reserved.
  Copyright (c) 2018, Adafruit Industries (adafruit.com)

  SPDX-License-Identifier: LGPL-2.1-or-later
*/

#include "variant.h"
#include "SEGGER_RTT.h"
#include "nrf.h"
#include "wiring_constants.h"
#include "wiring_digital.h"

// =============================================================================
// g_ADigitalPinMap
// Maps Arduino logical pin numbers (0..47) to nRF52840 physical GPIO numbers.
// Physical GPIO layout: P0.00–P0.31 → entries 0–31; P1.00–P1.15 → entries 32–47.
//
// Notes:
//   - P0.00 (XL1) and P0.01 (XL2) are the 32MHz HF crystal pins.
//     Mark them 0xff (unusable) to prevent accidental use.
//   - P0.09 (NFC1) and P0.10 (NFC2) are repurposed as GPIO via UICR
//     nfct-pins-as-gpios. The Adafruit bootloader must be compiled with
//     NFCT_PINS_AS_GPIOS=1 (confirmed by the existing NCS UICR config).
//   - P0.18 is the SWD reset pin (UICR gpio-as-nreset). Usable as GPIO
//     in application code (boot behavior only), listed normally.
//   - P0.25, P0.26 are UART TX/RX for Segger RTT/SWD passthrough on some
//     dev kits; on this custom board they are general purpose.
// =============================================================================
const uint32_t g_ADigitalPinMap[] = {
    // P0.00–P0.31
    0xff, // 0  P0.00  XL1               — HF crystal, do not use
    0xff, // 1  P0.01  XL2               — HF crystal, do not use
    2,    // 2  P0.02  PIN_EINK_RES      — E-Ink reset (active-low, idle HIGH)
    3,    // 3  P0.03  PIN_EINK_BUSY     — E-Ink busy (active-high; nRF pull-down at sleep)
    4,    // 4  P0.04  PIN_BATTERY_ADC   — battery ADC (AIN2)
    5,    // 5  P0.05  PIN_EXT_FLASH_HOLD — external flash hold
    6,    // 6  P0.06  PIN_BTN_F3        — button Enter/Confirm (ACTIVE_LOW, pull-up)
    7,    // 7  P0.07  PIN_EXT_FLASH_WP  — external flash write-protect
    8,    // 8  P0.08  PIN_BTN_F4        — button Left/Back (ACTIVE_LOW, pull-up)
    9,    // 9  P0.09  PIN_BTN_F2        — button Up [NFC1 as GPIO] (ACTIVE_LOW, pull-up)
    10,   // 10 P0.10  PIN_BTN_F5        — button Down [NFC2 as GPIO] (ACTIVE_LOW, pull-up)
    11,   // 11 P0.11  PIN_BTN_F6        — button Right (ACTIVE_LOW, pull-up)
    12,   // 12 P0.12  PIN_GNSS_PWR      — GP-02 LDO enable (active HIGH)
    13,   // 13 P0.13  PIN_LORA_PWR      — LoRa RA-01S LDO enable (active HIGH)
    14,   // 14 P0.14  PIN_LED_2         — LED Blue (ACTIVE_LOW)
    15,   // 15 P0.15  PIN_BATTERY_PWR   — battery divider enable (active HIGH)
    16,   // 16 P0.16  PIN_RTC_CLKOUT    — PCF8563 CLKOUT (not used by firmware)
    17,   // 17 P0.17  PIN_LORA_BUSY     — LoRa busy output (active-high)
    18,   // 18 P0.18  nRESET            — SWD reset / board reset button (UICR gpio-as-nreset)
    19,   // 19 P0.19  PIN_LORA_SCK      — LoRa SPI0 SCK
    20,   // 20 P0.20  PIN_LORA_DIO1     — LoRa TX/RX done IRQ (active-high)
    21,   // 21 P0.21  —
    22,   // 22 P0.22  PIN_LORA_MOSI     — LoRa SPI0 MOSI
    23,   // 23 P0.23  PIN_LORA_MISO     — LoRa SPI0 MISO
    24,   // 24 P0.24  PIN_LORA_CS       — LoRa chip select (active-low)
    25,   // 25 P0.25  PIN_LORA_RST      — LoRa reset (active-low, idle HIGH)
    26,   // 26 P0.26  PIN_WIRE_SDA      — I2C SDA (PCF8563 RTC @ 0x51)
    27,   // 27 P0.27  PIN_WIRE_SCL      — I2C SCL (PCF8563 RTC @ 0x51)
    28,   // 28 P0.28  PIN_EINK_DC       — E-Ink data/command (HIGH=data, LOW=cmd)
    29,   // 29 P0.29  PIN_EINK_MOSI     — E-Ink SPI1 MOSI
    30,   // 30 P0.30  PIN_EINK_CS       — E-Ink chip select (active-low)
    31,   // 31 P0.31  PIN_EINK_SCLK     — E-Ink SPI1 SCK

    // P1.00–P1.15
    32, // 32 P1.00  PIN_EXT_UART_RX    — external UART listen (RX-only, 1200 bps)
    33, // 33 P1.01  PIN_LED_1          — LED Green (ACTIVE_LOW)
    34, // 34 P1.02  PIN_GNSS_REGION    — GP-02 constellation selection
    35, // 35 P1.03  PIN_LED_0          — LED Red (ACTIVE_LOW)
    36, // 36 P1.04  PIN_GNSS_PPS       — GP-02 pulse-per-second output
    37, // 37 P1.05  PIN_GNSS_RST       — GP-02 hard reset (active HIGH via NPN)
    38, // 38 P1.06  —
    39, // 39 P1.07  PIN_GNSS_STBY      — GP-02 standby (active-low; HIGH=normal)
    40, // 40 P1.08  PIN_GNSS_TX        — UART1 TX → GP-02 RX
    41, // 41 P1.09  PIN_GNSS_RX        — UART1 RX ← GP-02 TX
    42, // 42 P1.10  PIN_EINK_EN        — E-Ink panel LDO enable (active HIGH)
    43, // 43 P1.11  PIN_BTN_F1         — button Fn/Cancel (ACTIVE_LOW, pull-up; hold ≥4s → shutdown)
    44, // 44 P1.12  PIN_EXT_FLASH_MOSI — external flash SPI MOSI
    45, // 45 P1.13  PIN_EXT_FLASH_MISO — external flash SPI MISO
    46, // 46 P1.14  PIN_EXT_FLASH_SCK  — external flash SPI SCK
    47, // 47 P1.15  PIN_EXT_FLASH_CS   — external flash chip select (active-low)
};

// =============================================================================
// initVariant
// Called by the Arduino framework after SoftDevice init, before setup().
// Only configure hardware that is definitely present.
// DO NOT call sd_power_dcdc_mode_set() — no external LC filter on this board.
// =============================================================================
void initVariant(void)
{
    // --- LEDs (all GPIO_ACTIVE_LOW: set HIGH/off before enabling output) ---
    pinMode(PIN_LED_0, OUTPUT);
    ledOff(PIN_LED_0);

    pinMode(PIN_LED_2, OUTPUT);
    ledOff(PIN_LED_2);

    pinMode(PIN_LED_1, OUTPUT);
    ledOff(PIN_LED_1);

#if defined(PIN_EINK_POWER)
    pinMode(PIN_EINK_DC, INPUT);
    pinMode(PIN_EINK_RES, INPUT);
    pinMode(PIN_EINK_CS, INPUT);
    pinMode(PIN_EINK_MOSI, INPUT);
    pinMode(PIN_EINK_SCLK, INPUT);
    pinMode(PIN_EINK_BUSY, INPUT_PULLDOWN);
    pinMode(PIN_EINK_POWER, OUTPUT);
    // Cut panel LDO last (active-LOW: !EINK_POWER_ACTIVE = OFF)
    digitalWrite(PIN_EINK_POWER, !EINK_POWER_ACTIVE);
#endif

#if defined(PIN_EXT_FLASH_CS)
    pinMode(PIN_EXT_FLASH_SCK, OUTPUT);
    digitalWrite(PIN_EXT_FLASH_SCK, LOW);
    pinMode(PIN_EXT_FLASH_MOSI, OUTPUT);
    digitalWrite(PIN_EXT_FLASH_MOSI, LOW);
    pinMode(PIN_EXT_FLASH_MISO, INPUT);
    pinMode(PIN_EXT_FLASH_CS, OUTPUT);
    digitalWrite(PIN_EXT_FLASH_CS, HIGH);
    pinMode(PIN_EXT_FLASH_WP, OUTPUT);
    digitalWrite(PIN_EXT_FLASH_WP, HIGH);
    pinMode(PIN_EXT_FLASH_HOLD, OUTPUT);
    digitalWrite(PIN_EXT_FLASH_HOLD, HIGH);
#endif

    SEGGER_RTT_printf(0, "initVariant: complete\n");
}

// =============================================================================
// variant_shutdown  (weak — optionally called before deep sleep)
// For Phase 0, nothing extra to do. As peripherals are added, add their
// shutdown/tristate here to avoid leakage current (see t-echo variant for
// E-Ink pin tri-stating example).
// =============================================================================
void variant_shutdown(void)
{
    // Turn off all LEDs before deep sleep.
    ledOff(PIN_LED_0);
    ledOff(PIN_LED_2);
    ledOff(PIN_LED_1);

#if defined(PIN_EINK_POWER)
    pinMode(PIN_EINK_DC, INPUT);
    pinMode(PIN_EINK_RES, INPUT);
    pinMode(PIN_EINK_CS, INPUT);
    pinMode(PIN_EINK_MOSI, INPUT);
    pinMode(PIN_EINK_SCLK, INPUT);
    pinMode(PIN_EINK_BUSY, INPUT_PULLDOWN);
    // Cut panel LDO last (active-LOW: !EINK_POWER_ACTIVE = OFF)
    digitalWrite(PIN_EINK_POWER, !EINK_POWER_ACTIVE);
#endif

#if defined(USE_SX1262) && defined(PIN_LORA_PWR)
    pinMode(PIN_LORA_RST, INPUT);
    pinMode(PIN_LORA_CS, INPUT);
    pinMode(PIN_LORA_MOSI, INPUT);
    pinMode(PIN_LORA_MISO, INPUT);
    pinMode(PIN_LORA_SCK, INPUT);
    pinMode(PIN_LORA_DIO1, INPUT_PULLDOWN);
    pinMode(PIN_LORA_BUSY, INPUT_PULLDOWN);
    digitalWrite(PIN_LORA_PWR, LOW);
#endif

#if defined(PIN_EXT_FLASH_CS)
    pinMode(PIN_EXT_FLASH_SCK, OUTPUT);
    digitalWrite(PIN_EXT_FLASH_SCK, LOW);
    pinMode(PIN_EXT_FLASH_MOSI, OUTPUT);
    digitalWrite(PIN_EXT_FLASH_MOSI, LOW);
    pinMode(PIN_EXT_FLASH_MISO, INPUT);
    pinMode(PIN_EXT_FLASH_CS, OUTPUT);
    digitalWrite(PIN_EXT_FLASH_CS, HIGH);
    pinMode(PIN_EXT_FLASH_WP, OUTPUT);
    digitalWrite(PIN_EXT_FLASH_WP, HIGH);
    pinMode(PIN_EXT_FLASH_HOLD, OUTPUT);
    digitalWrite(PIN_EXT_FLASH_HOLD, HIGH);
#endif

    SEGGER_RTT_printf(0, "variant_shutdown: Entering deep sleep mode\n");
}
