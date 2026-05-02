#pragma once

#define BUTTON_PIN 0
#define BATTERY_PIN 1
#define ADC_MULTIPLIER 5.65

#define I2C_SDA 3
#define I2C_SCL 4

#define GPS_RX_PIN 18
#define GPS_TX_PIN 17
#define GPS_EN 33
#define HAS_GPS 1

#define LED_PIN 16

#define USE_SX1268
#define LORA_SCK 12
#define LORA_MISO 13
#define LORA_MOSI 11
#define LORA_CS 10
#define LORA_DIO1 5
#define LORA_DIO2 6
#define LORA_RESET 9

#define SX126X_RXEN 42
#define SX126X_TXEN 14
#define SX126X_BUSY LORA_DIO2
#define SX126X_MAX_POWER 22 // Module amplifies to 30dBm

#define EXT_PWR_ENABLE 21 // Power for LoRa Radio
#define EXT_LORA_PWR 21

// Solar pins (if applicable)
#define SOLAR_CHARGE_DISABLE_PIN 48
#define SOLAR_VOLTAGE_READ_PIN 7
#define SOLAR_ADC_MULTIPLIER 10.33
