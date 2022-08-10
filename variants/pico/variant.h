// #define RADIOLIB_CUSTOM_ARDUINO 1
// #define RADIOLIB_TONE_UNSUPPORTED 1
// #define RADIOLIB_SOFTWARE_SERIAL_UNSUPPORTED 1

#define ARDUINO_ARCH_AVR

#define CBC 0
#define CTR 1
#define ECB 0

#define NO_GPS 1
#define USE_SH1106 1
#undef GPS_SERIAL_NUM

// #define I2C_SDA 6
// #define I2C_SCL 7

#define BUTTON_PIN 17
#define EXT_NOTIFY_OUT 4

#define BATTERY_PIN 26
// ratio of voltage divider = 3.0 (R17=200k, R18=100k)
#define ADC_MULTIPLIER 3.1 // 3.0 + a bit for being optimistic

#define USE_RF95
#define USE_SX1262

#undef RF95_SCK
#undef RF95_MISO
#undef RF95_MOSI
#undef RF95_NSS

#define RF95_SCK 10
#define RF95_MISO 12
#define RF95_MOSI 11
#define RF95_NSS 3

#define LORA_DIO0 RADIOLIB_NC
#define LORA_RESET 15
#define LORA_DIO1 20
#define LORA_DIO2 2
#define LORA_DIO3 RADIOLIB_NC

#ifdef USE_SX1262
#define SX126X_CS RF95_NSS
#define SX126X_DIO1 LORA_DIO1
#define SX126X_BUSY LORA_DIO2
#define SX126X_RESET LORA_RESET
#define SX126X_E22
#endif

#include <Adafruit_TinyUSB.h>