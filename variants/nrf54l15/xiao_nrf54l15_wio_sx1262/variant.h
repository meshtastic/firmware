#pragma once

/*
 * Seeed Studio XIAO nRF54L15 Sense + Wio-SX1262 for XIAO.
 *
 * The Wio module's standard XIAO header wiring is:
 *   D8  (P2.01 / pin 33) -> SCK
 *   D9  (P2.04 / pin 36) -> MISO
 *   D10 (P2.02 / pin 34) -> MOSI
 *   D4  (P1.10 / pin 26) -> NSS / CS
 *   D1  (P1.05 / pin 21) -> DIO1
 *   D3  (P1.07 / pin 23) -> BUSY
 *   D2  (P1.06 / pin 22) -> RESET
 *   D5  (P1.11 / pin 27) -> RF_SW1 / RXEN
 *
 * Pin numbers below use Meshtastic's nRF54L15 Arduino shim convention:
 * P0.n = n, P1.n = 16 + n, P2.n = 32 + n.
 */

#ifndef XIAO_NRF54L15_WIO_SX1262
#define XIAO_NRF54L15_WIO_SX1262
#endif

#define USE_SX1262
#define SX126X_CS 26
#define SX126X_DIO1 21
#define SX126X_BUSY 23
#define SX126X_RESET 22
#define SX126X_RXEN 27

// The Wio-SX1262 uses DIO2 for its TX RF-switch path and DIO3 to supply the
// module TCXO reference. RXEN stays on D5, as on Meshtastic's nRF52840 Wio kit.
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 1.8f
#define SX126X_MAX_POWER 22
#define REGULATORY_GAIN_LORA 7

// The Sense board has one active-low status LED on P2.00. The stock nRF54L15
// base profile has HAS_BUTTON=0, so D0 is left untouched for Wio add-ons.
#define PIN_LED1 32
#define LED_STATE_ON 0

// Meshtastic's nRF54L15 TwoWire shim uses i2c30. On XIAO that is the free
// D11/D12 bus (P0.03 SCL, P0.04 SDA); i2c22 is disabled in the overlay because
// it would otherwise claim D4/D5, which are required by the Wio-SX1262.
#define PIN_WIRE_SDA 4
#define PIN_WIRE_SCL 3
#define WIRE_INTERFACES_COUNT 1

// GPS on the XIAO header UART: Serial1 is bound to Zephyr uart21
// (TX = P2.08/D6, RX = P2.07/D7; enabled in the board overlay). Meshtastic
// probes module type and baud rate at runtime, so no fixed model is declared.
// GPS_RX_PIN/GPS_TX_PIN are required for NodeDB to default gps_mode to
// ENABLED (undefined RX pin means "no GPS fitted"); the actual pin muxing is
// owned by the device tree, and the shim's setPins() is a no-op.
#define HAS_GPS 1
#define GPS_RX_PIN 39 // P2.07 = D7
#define GPS_TX_PIN 40 // P2.08 = D6
// Seeed's "L76K GNSS Module for XIAO" shield routes the module's STANDBY
// pin to D0 (P1.04) and the module stays asleep (UART silent) until it is
// driven high. Matches the upstream seeed_xiao_nrf52840_kit variant; the
// default GPS_STANDBY_ACTIVE=LOW polarity is correct for the L76K.
#define PIN_GPS_STANDBY 20 // P1.04 = D0
#define GPS_BAUDRATE 9600
#define GPS_THREAD_INTERVAL 50
// The L76K streams NMEA (RX path verified) but does not answer probe
// commands on this stack - its reset line is shared with the Wio-SX1262's
// RESET on D2, so the command channel is unreliable. Parse the stream
// directly instead of requiring a successful model probe.
#define GPS_SKIP_PROBE 1
