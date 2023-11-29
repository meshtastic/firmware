// https://betafpv.com/products/elrs-micro-tx-module
#include <NeoPixelBus.h>

// 0.96" OLED
#define I2C_SDA 22
#define I2C_SCL 32

// NO GPS
#undef GPS_RX_PIN
#undef GPS_TX_PIN

#define LORA_SCK 18
#define LORA_MISO 19
#define LORA_MOSI 23
#define LORA_CS 5
#define RF95_FAN_EN 17

#define LED_PIN 16 // This is a LED_WS2812 not a standard LED

#define BUTTON_PIN 25
#define BUTTON_NEED_PULLUP

#undef EXT_NOTIFY_OUT

// SX128X 2.4 Ghz LoRa module
#define USE_SX1280
#define LORA_RESET 14
#define SX128X_CS 5
#define SX128X_DIO1 4
#define SX128X_BUSY 21
#define SX128X_TXEN 26
#define SX128X_RXEN 27
#define SX128X_RESET LORA_RESET
#define SX128X_MAX_POWER 13
