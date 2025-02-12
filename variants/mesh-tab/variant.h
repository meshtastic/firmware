#ifndef _VARIANT_MESHTAB_DIY_
#define _VARIANT_MESHTAB_DIY_

#define HAS_TOUCHSCREEN 1

#define SLEEP_TIME 120

// Analog pins
#define BATTERY_PIN 4 // A battery voltage measurement pin, voltage divider connected here to measure battery voltage
// ratio of voltage divider (100k, 220k)
#define ADC_MULTIPLIER 1.6 // 1.45 + 10% for correction of display undervoltage.
#define ADC_CHANNEL ADC1_GPIO4_CHANNEL

// LED
#define LED_PIN 21

// Button
#define BUTTON_PIN 0

// Button
#define PIN_BUZZER 5

// GPS
#define GPS_RX_PIN 18
#define GPS_TX_PIN 17

// #define HAS_SDCARD 1
#define SPI_MOSI 13
#define SPI_SCK 12
#define SPI_MISO 11
#define SPI_CS 10
#define SDCARD_CS 6

// LORA MODULES
#define USE_SX1262

// LORA SPI
#define LORA_SCK 36
#define LORA_MISO 37
#define LORA_MOSI 35
#define LORA_CS 39

// LORA CONFIG
#define LORA_DIO0 -1 // a No connect on the SX1262 module
#define LORA_RESET 14
#define LORA_DIO1 15 // SX1262 IRQ
#define LORA_DIO2 40 // SX1262 BUSY
#define LORA_DIO3    // Not connected on PCB, but internally on the TTGO SX1262

#define SX126X_CS LORA_CS
#define SX126X_DIO1 LORA_DIO1
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_BUSY LORA_DIO2
#define SX126X_RESET 14
#define SX126X_RXEN 47
#define SX126X_TXEN RADIOLIB_NC // Assuming that DIO2 is connected to TXEN pin

#endif
