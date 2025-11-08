// ============================
// For Meshtastic
// ============================

#ifndef _VARIANT_RAK3112_
#define _VARIANT_RAK3112_
#endif

// #define HAS_SDCARD
// #define SDCARD_USE_SPI1

#define USE_SSD1306

#define BATTERY_PIN WB_A0 // A battery voltage measurement pin, voltage divider connected here to measure battery voltage
// ratio of voltage divider = 2.0 (R42=100k, R43=100k)
#define ADC_MULTIPLIER 2.11 // 2.0 + 10% for correction of display undervoltage.
#define ADC_CHANNEL ADC1_GPIO1_CHANNEL

#define I2C_SDA PIN_WIRE_SDA // I2C pins for this board
#define I2C_SCL PIN_WIRE_SCL

#define I2C_SDA1 PIN_WIRE1_SDA
#define I2C_SCL1 PIN_WIRE1_SCL

// // The default Wire will be mapped to PMU and RTC
// static const uint8_t SDA = PIN_WIRE_SDA;
// static const uint8_t SCL = PIN_WIRE_SCL;

#define LED_PIN LED_GREEN // If defined we will blink this LED
#define BUTTON_PIN WB_IO5 // If defined, this will be used for user button presses,
#define ledOff(pin) pinMode(pin, INPUT)

#define BUTTON_NEED_PULLUP

#define USE_SX1262

#define LORA_SCK 5
#define LORA_MISO 3
#define LORA_MOSI 6
#define LORA_CS 7
#define LORA_RESET 8

// per SX1262_Receive_Interrupt/utilities.h
#ifdef USE_SX1262
#define SX126X_CS LORA_CS
#define SX126X_DIO1 47
#define SX126X_BUSY 48
#define SX126X_RESET LORA_RESET
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 1.8
#endif

// #define HAS_SDCARD // Have SPI interface SD card slot
// #define SDCARD_USE_SPI1