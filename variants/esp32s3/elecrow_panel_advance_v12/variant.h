#pragma once

#define I2C_SDA 15
#define I2C_SCL 16

#define DEVICE_BATTERY_INA_ADDRESS 0x40

// GPS via UART1 connector. These pins are shared with the radio-header reset
// and DIO1 signals, so an external GPS cannot be used at the same time.
#define GPS_DEFAULT_NOT_PRESENT 1
#define HAS_GPS 1
#define GPS_RX_PIN 19
#define GPS_TX_PIN 20

// CrowPanel Advance v1.2+ radio-header wiring.
#define USE_SX1262
#define LORA_CS 8
#define LORA_SCK 5
#define LORA_MISO 4
#define LORA_MOSI 6
#define LORA_RESET 19
#define LORA_DIO1 20
#define LORA_DIO2 2

#define SX126X_CS LORA_CS
#define SX126X_DIO1 LORA_DIO1
#define SX126X_BUSY LORA_DIO2
#define SX126X_RESET LORA_RESET
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 3.3
