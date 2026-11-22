#pragma once

// LilyGO T-Display-P4 V1.0, AMOLED assembly: ESP32-P4 + ESP32-C6 (esp_hosted over SDIO), SX1262,
// L76K GPS, RM69A10 MIPI-DSI panel + GT9895 touch. Radio RESET/DIO1 and all power/reset lines sit
// on an XL9535 I2C expander. Not supported: LR2021 radio, TFT (HI8561) assembly, V2.0 board.

// ---- I2C bus A (Wire, 400 kHz): XL9535 0x20, PCF8563 0x51, BQ27220 0x55, GT9895 0x5D ----
#define I2C_SDA 7
#define I2C_SCL 8
// Bus B (GPIO20/21: ES8311, SGM38121, AW86224, ICM20948) is intentionally not defined: unused.

// ---- XL9535 expander (PCA9555 register map), driver in src/platform/esp32/ExtensionIOXL9535 ----
#define USE_XL9535
#define XL9535_ADDR 0x20
#define XL9535_VPIN_BASE 100 // RadioLib virtual pin = XL9535_VPIN_BASE + linear index
#define XL9535_INT_PIN 5     // ALL_INT, open drain, active low, 10k pull-up. Unused.
// Linear index 0..15 = P00..P07, P10..P17 (LilyGO kIo0..kIo7 -> 0..7, kIo10..kIo17 -> 8..15)
#define EXIO_3V3_EN_N 0    // IO0  out, 0 = Low_3V3 ON. Never 1: the P4 can lock in download mode.
#define EXIO_ANT_SEL 1     // IO1  out, 1 = internal antenna, 0 = MMCX1
#define EXIO_DISP_RST 2    // IO2  out, active low
#define EXIO_TOUCH_RST 3   // IO3  out, active low
#define EXIO_TOUCH_INT 4   // IO4  in  (unused)
#define EXIO_ETH_RST 5     // IO5  out, 0 = PHY in reset
#define EXIO_AUDIO_PWR 6   // IO6  out, 1 = OUT_5V on
#define EXIO_IMU_INT 7     // IO7  in  (unused)
#define EXIO_USB_PHY_EN 8  // IO10 out, LilyGO writes 1. TODO(hw-verify): the schematic FET suggests the opposite polarity
#define EXIO_GPS_WAKE 9    // IO11 out, 1 = awake
#define EXIO_RTC_INT 10    // IO12 in  (unused)
#define EXIO_C6_WAKE 11    // IO13 in  (unused)
#define EXIO_C6_EN 12      // IO14 out, 1 = C6 runs, 0 = reset
#define EXIO_SD_PWR_N 13   // IO15 out, 0 = SD VDD on
#define EXIO_LORA_NRST 14  // IO16 out, active low
#define EXIO_LORA_DIO1 15  // IO17 in,  active high

// The expander driver forces these pins to output-low on every register write
#define XL9535_LOCKED_LOW_MASK (1u << EXIO_3V3_EN_N)
// Boot state for writePorts(). Outputs: 0,1,2,3,5,6,8,9,12,13,14; inputs: 4,7,10,11,15
#define TDP4_XIO_OUTPUT_MASK 0x736F
// Latches 0:0 1:1 2:0 3:0 5:0 6:0 8:1 9:1 12:0 13:1 14:0, input bits = 1
#define TDP4_XIO_BOOT_LATCH 0xAF92
// Must be a verified output-high before a radio is created (internal antenna selected)
#define XL9535_LORA_REQUIRED_HIGH_PIN EXIO_ANT_SEL

// ---- LoRa SX1262 (HPD16A module), SPI used by the radio only ----
#define USE_SX1262
#define LORA_SCK 2
#define LORA_MOSI 3
#define LORA_MISO 4
#define LORA_CS 24
#define LORA_DIO1 (XL9535_VPIN_BASE + EXIO_LORA_DIO1)  // 115, virtual
#define LORA_RESET (XL9535_VPIN_BASE + EXIO_LORA_NRST) // 114, virtual
#define SX126X_CS LORA_CS
#define SX126X_DIO1 LORA_DIO1
#define SX126X_BUSY 6
#define SX126X_RESET LORA_RESET
#define SX126X_DIO2_AS_RF_SWITCH     // TODO(hw-verify): module-internal switch; DIO2 is not routed on the board
#define SX126X_DIO3_TCXO_VOLTAGE 1.6 // TODO(hw-verify): LilyGO default; TCXO_OPTIONAL falls back to XTAL
#define TCXO_OPTIONAL
#define LORA_DIO1_SOFTWARE_POLL 1 // no MCU interrupt for DIO1: poll the SX126x IRQ status over SPI
#define LORA_DIO1_EXTENDED_IO     // keeps sleep.cpp away from the virtual pins

// ---- GPS L76K on Serial1 (WAKE_UP = EXIO_GPS_WAKE, held high). Baud is probed. ----
#define GPS_RX_PIN 22 // MCU RX <- L76K TXD
#define GPS_TX_PIN 23 // MCU TX -> L76K RXD

// ---- RTC ----
#define PCF8563_RTC 0x51

// ---- Power: no power latch, and deep sleep is replaced by a soft-off in variant_shutdown() ----
#define HAS_CPU_SHUTDOWN 0
