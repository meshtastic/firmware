#pragma once

// ESP32-S3 Super Mini + Wio-SX1262 V1.0 Custom Variant
// Pinmap custom ya soldado

#define LED_PIN 15
#define LED_STATE_ON 1

#define BUTTON_PIN 0
#define BUTTON_NEED_PULLUP

#define I2C_SDA 17
#define I2C_SCL 18

#define USE_SX1262

// SX1262 SPI pins (custom soldered pinmap)
#define LORA_SCK 12
#define LORA_MISO 14
#define LORA_MOSI 11
#define LORA_CS 10

#define LORA_RESET 5
#define LORA_DIO1 1
#define LORA_DIO2 4
#define LORA_DIO3 3

#define SX126X_CS LORA_CS
#define SX126X_DIO1 LORA_DIO1
#define SX126X_BUSY RADIOLIB_NC
#define SX126X_RESET LORA_RESET

// TCXO 1.8V on DIO3
#define SX126X_DIO3_TCXO_VOLTAGE 1.8

// DIO2 as RF switch (internal to SX1262)
#define SX126X_DIO2_AS_RF_SWITCH

// External RF switch control via MCU pins
#define SX126X_RXEN 13
#define SX126X_TXEN RADIOLIB_NC

// GPS (if connected)
#undef GPS_RX_PIN
#undef GPS_TX_PIN
#define GPS_RX_PIN 9
#define GPS_TX_PIN 8

// Battery ADC
#define BATTERY_PIN 2
#define BATTERY_VOLTAGE_DIVIDER 2

// Board identification
#define HAS_DISPLAY 0
#define HAS_GPS 0