#pragma once

/*
 * Nordic nRF54L15-DK (PCA10156) - Meshtastic variant
 *
 * ── GPIO voltage domains ─────────────────────────────────────────────────────
 *   P0  (gpio0 @ 0x10A000)  Main domain     3.0 V  ← usable
 *   P1  (gpio1 @ 0xd8200 )  LP domain       1.8 V  ← NOT compatible with E22
 *   P2  (gpio2 @ 0x50400 )  HP domain       3.0 V  ← usable
 *
 * The SX1262 needs VIH ≥ 0.7 × VDD = 2.31 V (VDD = 3.3 V).
 * P1 outputs only 1.8 V → chip stays in reset, BUSY never goes LOW.
 * All E22 signals are therefore on P2 (3.0 V), driven by SPIM00.
 *
 * EBYTE E22-900M30S (SX1262) wiring - J2 header, all P2:
 *
 *   E22 pin    GPIO      pin#  Notes
 *   ─────────────────────────────────────────────────────────────────────
 *   MISO    →  P2.04      36   SPIM00 data in
 *   NSS/CS  →  P2.05      37   SPI chip-select (RadioLib GPIO)
 *   DIO1    →  P2.06      38   IRQ - interrupt via gpiote30
 *   BUSY    →  P2.03      35   GPIO input
 *   NRESET  →  P2.00      32   GPIO output
 *   RXEN    →  P2.07      39   Held HIGH via ANT_SW (LNA always active)
 *   MOSI    →  P2.02      34   SPIM00 data out
 *   SCK     →  P2.01      33   SPIM00 clock
 *
 *   DIO2 → TXEN bridge required on E22 module (solder bridge / wire).
 *   DIO3 drives TCXO reference (1.8 V).
 *
 * Pin numbering convention: P0.n = n, P1.n = 16+n, P2.n = 32+n.
 *
 * Reserved / do-not-use DK pins:
 *   P0.00-P0.02  IMCU VCOM TX/RX/RTS pads (uart30 disabled; pads idle)
 *   P0.03        I2C SDA (TWIM30) - sensor bus
 *   P0.04        I2C SCL (TWIM30) - sensor bus; SW3 button on this pad,
 *                DO NOT press SW3 while I2C is active
 *   P1.00-P1.01  32 kHz crystal
 *   P1.02-P1.03  NFC antenna
 *   P1.10        LED1 (status LED - keep)
 *   P1.13        BTN0 - main user button
 *   P1.14        LED3
 *   P2.01-P2.05  SPIM00 / E22 (see above)
 *   P2.08-P2.10  Trace pins (avoid)
 */

#ifndef NRF54L15_DK
#define NRF54L15_DK
#endif

// ── SX1262 / E22-900M30S - all P2, HP domain (3.0 V) ────────────────────────
#define USE_SX1262
#define SX126X_CS 37    // P2.05 - chip-select
#define SX126X_DIO1 38  // P2.06 - IRQ (gpiote30 capable)
#define SX126X_BUSY 35  // P2.03 - BUSY
#define SX126X_RESET 32 // P2.00 - NRESET

// RXEN (P2.07) held HIGH permanently - LNA always active.
// RadioLib must NOT toggle it; ANT_SW drives it HIGH before lora.begin().
#define SX126X_ANT_SW 39 // P2.07 - RXEN driven HIGH at init

// DIO2 controls TXEN via bridge on E22 module.
// DIO3 provides 1.8 V TCXO reference.
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 1.8f

// ── LEDs (active HIGH) ───────────────────────────────────────────────────────
#define PIN_LED1 26 // P1.10 - LED1 (status LED, LP domain - output only, OK)
#define PIN_LED2 41 // P2.09 - LED0 on DK (remapped; P2.07 now used for RXEN)
#define LED_STATE_ON 1

// ── Buttons (active LOW, internal pull-up) ───────────────────────────────────
// BTN1 (P1.09), BTN2 (P1.08) and BTN3 (P0.04) deleted from DTS - only BTN0
// remains. BTN3's pad (P0.04) is now I2C SCL.
#define PIN_BUTTON1 29 // P1.13 - BTN0
#define BUTTON_NEED_PULLUP

// ── I2C bus (TWIM30, HP domain, 3.0 V) ──────────────────────────────────────
// SDA=P0.03, SCL=P0.04. Pinctrl + clock-frequency live in the board overlay.
// External 4.7 kΩ pull-ups required on both lines. Meshtastic's Arduino
// TwoWire layer (src/platform/nrf54l15/Wire.cpp) resolves the device at
// compile time via DT_NODELABEL(i2c30); these PIN_WIRE_* defines are kept
// for parity with the Arduino convention used by other variants.
#define PIN_WIRE_SDA 3 // P0.03
#define PIN_WIRE_SCL 4 // P0.04
#define WIRE_INTERFACES_COUNT 1
