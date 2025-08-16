/*
 * variant.h for M5Stack CoreS3 with SX1262 radio
 */

#pragma once

// --- GPS ---
#define HAS_GPS 0
#undef GPS_RX_PIN
#undef GPS_TX_PIN

// --- I2C (AXP2101, display, etc.) ---
#define I2C_SDA 12
#define I2C_SCL 11

// --- LoRa (SX1262) ---
#undef LORA_SCK
#undef LORA_MISO
#undef LORA_MOSI
#undef LORA_CS

#define LORA_SCK   36
#define LORA_MISO  35
#define LORA_MOSI  37
#define LORA_CS    1   // NSS

#define USE_SX1262

#define LORA_DIO1  10  // IRQ line for SX1262
#define LORA_BUSY  2   // BUSY line
#define LORA_RST   7   // RESET line
#define LORA_RESET LORA_RST

// SX126x driver aliases
#ifdef USE_SX1262
  #define SX126X_CS    LORA_CS
  #define SX126X_DIO1  LORA_DIO1
  #define SX126X_BUSY  LORA_BUSY
  #define SX126X_RESET LORA_RESET
  #define SX126X_DIO2_AS_RF_SWITCH
  #define SX126X_DIO3_TCXO_VOLTAGE 1.8
#endif

// --- Power management ---
#define HAS_AXP2101

// Debug breadcrumb (remove later)
#warning "CoreS3 M5 Stack Variant: SX1262 pins CS=1, DIO1=10, BUSY=2, RST=7"