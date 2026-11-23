#pragma once

#define USE_SSD1306 // SSD1315 is SSD1306-compatible
#define DISPLAY_FLIP_SCREEN
#define I2C_SDA 5
#define I2C_SCL 4
#define VEXT_ENABLE 45 // Active-low OLED supply
#define PERIPHERAL_WARMUP_MS 100

#define BUTTON_PIN 0

#define BATTERY_PIN 8
#define ADC_CHANNEL ADC_CHANNEL_7
#define ADC_MULTIPLIER 2.0f // Factory battery divider scaling

#define HAS_NEOPIXEL
#define NEOPIXEL_COUNT 1
#define NEOPIXEL_DATA 48
#define NEOPIXEL_TYPE (NEO_GRB + NEO_KHZ800)

// The AM36 module routes LR2021 DIO7 to ESP32-S3 GPIO21.
#define USE_LR2021
#define LORA_CS 39
#define LORA_SCK 40
#define LORA_MOSI 41
#define LORA_MISO 42
#define LORA_DIO1 21

#define LR2021_SPI_NSS_PIN LORA_CS
#define LR2021_IRQ_PIN LORA_DIO1
#define LR2021_NRESET_PIN 38
#define LR2021_BUSY_PIN 17 // Internal AM36 signal, unavailable for expansion I2C
#define LR2021_IRQ_DIO_NUM 7
#define LR2021_DIO_AS_RF_SWITCH
#define LR2021_MAX_POWER 22
#define LR2021_MAX_POWER_HF 8
