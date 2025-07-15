#ifndef Pins_Arduino_h
#define Pins_Arduino_h

#include <stdint.h>

#define PRIVATE_HW
#define DIY_V1
#define EBYTE_E22

// Serial Pins
static const uint8_t TX = 15;  // txd1
static const uint8_t RX = 14;  // rxen

#define TX1 15  // txd1
#define RX1 14  // rxen



// LoRa SPI Pins (VSPI)
static const uint8_t SCK = 5;   // sck
static const uint8_t MISO = 19; // miso
static const uint8_t MOSI = 27; // mosi
static const uint8_t SS = 18;   // nss

// Ethernet SPI Pins (HSPI)
static const uint8_t ETHERNET_SCK = 35;   // sclka
static const uint8_t ETHERNET_MISO = 34;  // misoa
static const uint8_t ETHERNET_MOSI = 25;  // mosia
static const uint8_t ETHERNET_CS = 16;    // ssa

// LoRa SX1262 Pins
#define LORA_DIO0 RADIOLIB_NC
#define LORA_RESET 23   // reset
#define LORA_DIO1 33    // di01
#define LORA_DIO2 32    // busy
#define LORA_DIO3 RADIOLIB_NC
#define SX126X_ANT_SW 13  // txen
#define SX126X_CS SS      // nss
#define SX126X_DIO1 LORA_DIO1
#define SX126X_BUSY LORA_DIO2
#define SX126X_RESET LORA_RESET
#define SX126X_POWER_EN 14 // rxen
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 1.8

// Meshtastic-Specific Defines
#define I2C_SDA 21
#define I2C_SCL 22

#undef GPS_RX_PIN
#define GPS_RX_PIN RX1
#undef GPS_TX_PIN
#define GPS_TX_PIN TX1

#define PIN_VBAT 36  // WB_A0
#define BATTERY_PIN PIN_VBAT
#define ADC_CHANNEL ADC1_GPIO36_CHANNEL

#undef LORA_SCK
#define LORA_SCK SCK
#undef LORA_MISO
#define LORA_MISO MISO
#undef LORA_MOSI
#define LORA_MOSI MOSI
#undef LORA_CS
#define LORA_CS SS

#define USE_SX1262

#endif /* Pins_Arduino_h *//
