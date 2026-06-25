// Shared pin/feature defines for BQ Station G2 and G3.
// Boards differ only in PA output power (SX126X_MAX_POWER) and the wiki URL.
#pragma once

// Station G2/G3 may not have GPS installed, but have a GROVE GPS Socket for an optional GPS module
#define GPS_RX_PIN 7
#define GPS_TX_PIN 15

// 1.3 inch OLED Screen
#define USE_SH1107_128_64

#define I2C_SDA 5
#define I2C_SCL 6

#define BUTTON_PIN 38 // Program button
#define BUTTON_NEED_PULLUP

#define USE_SX1262

#define LORA_MISO 14
#define LORA_SCK 12
#define LORA_MOSI 13
#define LORA_CS 11

#define LORA_RESET 21
#define LORA_DIO1 48

#ifdef USE_SX1262
#define SX126X_CS LORA_CS // Compatibility alias; prefer LORA_CS in board definitions.
#define SX126X_DIO1 LORA_DIO1
#define SX126X_BUSY 47
#define SX126X_RESET LORA_RESET

// DIO2 controls an antenna switch and the TCXO voltage is controlled by DIO3
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 1.8
// NOTE: SX126X_MAX_POWER is intentionally NOT defined here — each board sets it.
#endif
