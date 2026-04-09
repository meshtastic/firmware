#pragma once

/*
 * Nordic nRF54L15-DK (PCA10156) — Meshtastic variant
 *
 * EBYTE E22-900M30S (SX1262) wiring:
 *
 *   E22 pin    DK pin   GPIO     Notes
 *   ──────────────────────────────────────────────────────────────────────
 *   NSS/CS  →  J1 P0.09  (9)     SPI chip-select (RadioLib GPIO)
 *   SCK     →  J1 P1.12  (28)    SPIM20 clock
 *   MOSI    →  J1 P1.11  (27)    SPIM20 data out
 *   MISO    →  J1 P0.11  (11)    SPIM20 data in
 *   DIO1    →  J1 P0.08  (8)     IRQ — interrupt capable
 *   BUSY    →  J1 P0.07  (7)     GPIO input
 *   NRESET  →  J1 P0.06  (6)     GPIO output
 *   RXEN    →  J1 P0.05  (5)     Held HIGH via ANT_SW (LNA always on)
 *
 *   DIO2 → TXEN bridge required on E22 module (solder bridge / wire).
 *   DIO3 drives TCXO reference (1.8 V).
 *
 * Pin numbering convention used here: P0.n = n, P1.n = 16+n, P2.n = 32+n.
 * Validate against the Zephyr Arduino core variant map before building.
 *
 * Reserved DK pins (do NOT use):
 *   P0.00-P0.03  UART0 debug console (IMCU)
 *   P0.04        BTN3
 *   P1.00-P1.01  32 kHz crystal
 *   P1.02-P1.03  NFC antenna
 *   P1.04-P1.07  UART1
 *   P1.08        BTN2
 *   P1.09        BTN1
 *   P1.10        LED1
 *   P1.13        BTN0
 *   P1.14        LED3
 *   P2.00-P2.05  QSPI flash
 *   P2.07        LED2
 *   P2.09        LED0
 */

#ifndef NRF54L15_DK
#define NRF54L15_DK
#endif

// ── SX1262 / E22-900M30S ─────────────────────────────────────────────────
#define SX126X_CS           9   // P0.09 — chip-select
#define SX126X_DIO1         8   // P0.08 — IRQ
#define SX126X_BUSY         7   // P0.07 — BUSY
#define SX126X_RESET        6   // P0.06 — NRESET

// RXEN (P0.05) held HIGH permanently — LNA always active.
// RadioLib must NOT toggle it; use ANT_SW instead of RXEN.
#define SX126X_ANT_SW       5   // P0.05 — RXEN driven HIGH before lora.begin()

// DIO2 controls TXEN via bridge on E22 module.
// DIO3 provides 1.8 V TCXO reference.
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 1.8f

// ── LEDs (active HIGH) ───────────────────────────────────────────────────
#define PIN_LED1            26  // P1.10
#define PIN_LED2            39  // P2.07
#define LED_STATE_ON         1

// ── Buttons (active LOW, internal pull-up) ───────────────────────────────
#define PIN_BUTTON1         29  // P1.13 — BTN0
#define PIN_BUTTON2         25  // P1.09 — BTN1
#define BUTTON_NEED_PULLUP
