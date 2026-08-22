#pragma once

// WaveShark Radio is a headless device without a GPS/GNSS receiver.
#define HAS_SCREEN 0
#define HAS_GPS 0
#define HAS_BUTTON 0

// I2C
#ifndef USE_JTAG
#define I2C_SDA 4
#define I2C_SCL 15
#endif

// User-visible status LEDs
#define LED_POWER 25
#define LED_LORA 16

// Semtech SX1276 / RF95-class LoRa transceiver
#define USE_RF95

// Explicitly specify all LoRa SPI pins rather than relying on ESP32 defaults.
#define LORA_SCK 5
#define LORA_MISO 19
#define LORA_MOSI 27
#define LORA_CS 18

#ifndef USE_JTAG
#define LORA_RESET 14
#endif

#define LORA_DIO0 26
#define LORA_DIO1 35 // https://www.thethingsnetwork.org/forum/t/big-esp32-sx127x-topic-part-3/18436
#define LORA_DIO2 34 // Not really used

// Battery voltage divider: R9 = 47k, R13 = 56k.
// Ratio of voltage divider = (47 + 56) / 56 = 103 / 56 = ~1.84
// GPIO12 enables the switched divider; GPIO32 is ADC1 channel 4
#define ADC_MULTIPLIER (103.0 / 56.0)
#define BATTERY_PIN 32
#define ADC_CHANNEL ADC_CHANNEL_4 // GPIO32 = ADC2 channel 4 on ESP32
#define ADC_CTRL 12
#define ADC_CTRL_ENABLED HIGH

// USB power sensing
// R19 = 56k, R18 = 47k.
// 5 V USB produces approximately 2.28 V at GPIO33.
#define EXT_PWR_DETECT_ADC 33
#define EXT_PWR_DETECT_ADC_CHANNEL ADC_CHANNEL_5
#define EXT_PWR_DETECT_ADC_THRESHOLD_MV 1500

// Infer charging from USB presence and battery state of charge.
#define INFER_CHARGING_FROM_EXT_PWR
