#pragma once

// Meshtastic
#define HAS_SCREEN 0

// GPS
#define HAS_GPS 0 // This variant does not have GPS support
#undef GPS_RX_PIN
#undef GPS_TX_PIN

// Ethernet
#define HAS_ETHERNET 1                  // This variant has Ethernet support
#define ETH_PHY_TYPE ETH_PHY_LAN8720    // LAN8720 PHY
#define ETH_PHY_ADDR 1                  // PHY address
#define ETH_PHY_MDC 23                  // MDC pin
#define ETH_PHY_MDIO 18                 // MDIO pin
#define ETH_PHY_POWER 16                // Power pin
#define ETH_CLK_MODE ETH_CLOCK_GPIO0_IN // Clock mode

// LoRa
#define USE_SX1262
#define LORA_SCK 14  // Serial Clock
#define LORA_MISO 12 // Master In Slave Out
#define LORA_MOSI 15 // Master Out Slave In
#define LORA_CS 2    // Chip Select
#define LORA_DIO1 39 // DIO1 pin
#define LORA_RESET 4 // Reset pin
#define LORA_BUSY 36 // Busy pin

// SX1262
#define SX126X_CS LORA_CS
#define SX126X_DIO1 LORA_DIO1
#define SX126X_RESET LORA_RESET
#define SX126X_BUSY LORA_BUSY

// E22-900M30S
#define SX126X_RXEN 17
#define SX126X_TXEN RADIOLIB_NC
#define SX126X_DIO2_AS_RF_SWITCH 1 // Uses DIO2 as RF switch control
#define SX126X_DIO3_TCXO_VOLTAGE 1.8 // DIO3 used for TCXO voltage control (1.8V)
#define SX126X_MAX_POWER 22

// i2c
#define I2C_SDA 32 // I2C Data
#define I2C_SCL 33 // I2C Clock

// Button
#define BUTTON_PIN 35        // Button pin
#define BUTTON_NEED_PULLUP 1 // Button needs pull-up resistor