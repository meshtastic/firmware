#pragma once

// Meshtastic
#define IS_STATION 1
#define HAS_SCREEN 1
#define SCREEN_TYPE_SSD1306
#define SCREEN_SDA I2C_SDA
#define SCREEN_SCL I2C_SCL
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define SCREEN_RESET -1

// GPS
#define HAS_GPS 0
#define GPS_RX_PIN -1
#define GPS_TX_PIN -1

// Ethernet
#define HAS_ETHERNET 1
#define ETH_PHY_TYPE ETH_PHY_LAN8720
#define ETH_PHY_ADDR 1
#define ETH_PHY_MDC 23
#define ETH_PHY_MDIO 18
#define ETH_PHY_POWER 16
#define ETH_CLK_MODE ETH_CLOCK_GPIO0_IN

// LoRa
#define HAS_LORA 1
#define USE_SX1262
#define LORA_SCK 14
#define LORA_MISO 12
#define LORA_MOSI 15
#define LORA_CS 2
#define LORA_DIO1 39
#define LORA_RESET 4
#define LORA_BUSY 36

// SX1262
#define SX126X_CS LORA_CS
#define SX126X_DIO1 LORA_DIO1
#define SX126X_RESET LORA_RESET
#define SX126X_BUSY LORA_BUSY

// E22-900M30S
#define LORA_RXEN -1
#define LORA_TXEN -1
#define SX126X_DIO2_AS_RF_SWITCH 1
#define SX126X_DIO3_TCXO_VOLTAGE 1.8
#define LORA_TX_CURRENT_LIMIT 140.0
#define SX126X_MAX_POWER 30

// i2c
#define HAS_I2C 1
#define I2C_SDA 32
#define I2C_SCL 33