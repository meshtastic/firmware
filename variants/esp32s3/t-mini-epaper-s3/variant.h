#pragma once

#define GPS_DEFAULT_NOT_PRESENT 1

// SD card (TF)
#define HAS_SDCARD
#define SDCARD_USE_SPI1
#define SDCARD_CS 40
#define SD_SPI_FREQUENCY 25000000U

// Built-in RTC (I2C)
#define PCF8563_RTC 0x51
#define I2C_SDA 18
#define I2C_SCL 9

// Battery voltage monitoring
#define BATTERY_PIN 2 // A battery voltage measurement pin, voltage divider connected here to
// measure battery voltage ratio of voltage divider = 2.0 (assumption)
#define ADC_MULTIPLIER 2.11 // 2.0 + 10% for correction of display undervoltage.
#define ADC_CHANNEL ADC1_GPIO2_CHANNEL

// Display (E-Ink)
#define PIN_EINK_EN 42
#define PIN_EINK_CS 13
#define PIN_EINK_BUSY 10
#define PIN_EINK_DC 12
#define PIN_EINK_RES 11
#define PIN_EINK_SCLK 14
#define PIN_EINK_MOSI 15
#define DISPLAY_FORCE_SMALL_FONTS

// Rocker-style input (left/right + boot as press)
#define INPUTDRIVER_ENCODER_TYPE 2
#define INPUTDRIVER_ENCODER_UP 4
#define INPUTDRIVER_ENCODER_DOWN 3
#define INPUTDRIVER_ENCODER_BTN 0
#define UPDOWN_LONG_PRESS_REPEAT_INTERVAL 150

// Deep-sleep wake source
#define BUTTON_PIN INPUTDRIVER_ENCODER_BTN

// LoRa (SX1262)
#define USE_SX1262

#define LORA_DIO0 RADIOLIB_NC
#define LORA_DIO1 5
#define LORA_SCK 8
#define LORA_MISO 6
#define LORA_MOSI 17
#define LORA_CS 7
#define LORA_RESET 21
#define LORA_DIO2 16

#define SX126X_CS LORA_CS
#define SX126X_DIO1 LORA_DIO1
#define SX126X_BUSY LORA_DIO2
#define SX126X_RESET LORA_RESET
#define SX126X_DIO3_TCXO_VOLTAGE 1.8
