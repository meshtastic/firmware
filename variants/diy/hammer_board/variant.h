// variants/diy/hammer_board/variant.h
#ifndef VARIANT_H
#define VARIANT_H

#ifdef DIY_V1
// OLED
#define I2C_SDA 21
#define I2C_SCL 22
// GPS
#undef GPS_RX_PIN
#undef GPS_TX_PIN
#define GPS_RX_PIN 12
#define GPS_TX_PIN 15
#define GPS_UBLOX
// Power and Button
#define EXT_PWR_DETECT 4
#define BUTTON_PIN 39
#define SECOND_BUTTON_PIN 0 // Second button on IO0
// #define BATTERY_PIN 35 // Removed (no battery)
#define ADC_CHANNEL ADC1_GPIO35_CHANNEL
#define ADC_MULTIPLIER 1.85
// LoRa (VSPI)
#define LORA_SCK 5
#define LORA_MISO 19
#define LORA_MOSI 27
#define LORA_CS 18
#define LORA_DIO0 RADIOLIB_NC
#define LORA_RESET 23
#define LORA_DIO1 33
#define LORA_DIO2 32
#define LORA_DIO3 RADIOLIB_NC
#define SX126X_CS 18
#define SX126X_DIO1 33
#define SX126X_BUSY 32
#define SX126X_RESET 23
#define SX126X_RXEN 14
#define SX126X_TXEN 13
#define SX126X_DIO3_TCXO_VOLTAGE 1.8
#define TCXO_OPTIONAL
#define SX126X_MAX_POWER 22
// Ethernet (HSPI)
#define ETH_CS_PIN 16
#define ETH_SCLK_PIN 35
#define ETH_MISO_PIN 34
#define ETH_MOSI_PIN 25
#define ETH_RST_PIN 17
#define ETH_INT_PIN -1
#define ETH_ADDR 0
#define SPI3_HOST HSPI_HOST
// LoRa Modules
#define USE_SX1262
#define USE_SX1268
#define USE_LLCC68
#define USE_RF95
#ifdef EBYTE_E22
#define SX126X_DIO3_TCXO_VOLTAGE 1.8
#define TCXO_OPTIONAL
#endif
#endif // DIY_V1

#endif // VARIANT_H