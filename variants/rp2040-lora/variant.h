// #define RADIOLIB_CUSTOM_ARDUINO 1
// #define RADIOLIB_TONE_UNSUPPORTED 1
// #define RADIOLIB_SOFTWARE_SERIAL_UNSUPPORTED 1

#define ARDUINO_ARCH_AVR

// #define USE_SH1106 1

// default I2C pins:
// SDA = 4
// SCL = 5

// Recommended pins for SerialModule:
// txd = 8
// rxd = 9

#define EXT_NOTIFY_OUT 22
#define BUTTON_PIN 17

#define LED_PIN PIN_LED

// #define BATTERY_PIN 26
//  ratio of voltage divider = 3.0 (R17=200k, R18=100k)
// #define ADC_MULTIPLIER 3.1 // 3.0 + a bit for being optimistic

#define USE_SX1262

#undef LORA_SCK
#undef LORA_MISO
#undef LORA_MOSI
#undef LORA_CS

// https://www.waveshare.com/rp2040-lora.htm
// https://www.waveshare.com/img/devkit/RP2040-LoRa-HF/RP2040-LoRa-HF-details-11.jpg
#define LORA_SCK 14  // 10
#define LORA_MISO 24 // 12
#define LORA_MOSI 15 // 11
#define LORA_CS 13   // 3

#define LORA_DIO0 RADIOLIB_NC
#define LORA_RESET 23 // 15
#define LORA_DIO1 16  // 20
#define LORA_DIO2 18  // 2
#define LORA_DIO3 RADIOLIB_NC
#define LORA_DIO4 17

#ifdef USE_SX1262
#define SX126X_CS LORA_CS
#define SX126X_DIO1 LORA_DIO1
#define SX126X_BUSY LORA_DIO2
#define SX126X_RESET LORA_RESET
#define SX126X_DIO2_AS_RF_SWITCH
// #define SX126X_DIO3_TCXO_VOLTAGE 1.8
#endif