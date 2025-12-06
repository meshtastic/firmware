// #define RADIOLIB_CUSTOM_ARDUINO 1
// #define RADIOLIB_TONE_UNSUPPORTED 1
// #define RADIOLIB_SOFTWARE_SERIAL_UNSUPPORTED 1

#define ARDUINO_ARCH_AVR

// #define USE_SSD1306

// #define USE_SH1106 1

// default I2C pins:
// SDA = 4
// SCL = 5

// Recommended pins for SerialModule:
// txd = 8
// rxd = 9

#define EXT_NOTIFY_OUT 22
#define BUTTON_PIN 7
// #define BUTTON_NEED_PULLUP

#define LED_PIN PIN_LED

// #define BATTERY_PIN 26
//  ratio of voltage divider = 3.0 (R17=200k, R18=100k)
// #define ADC_MULTIPLIER 3.1 // 3.0 + a bit for being optimistic

#define USE_RF95 // RFM95/SX127x

#undef LORA_SCK
#undef LORA_MISO
#undef LORA_MOSI
#undef LORA_CS

// https://www.adafruit.com/product/5714
// https://learn.adafruit.com/feather-rp2040-rfm95
// https://learn.adafruit.com/assets/120283
// https://learn.adafruit.com/assets/120813
#define LORA_SCK 14  // 10 12P
#define LORA_MISO 8  // 12 10P
#define LORA_MOSI 15 // 11 11P
#define LORA_CS 16   // 3 13P

#define LORA_RESET 17 // 15 14P

#define LORA_DIO0 21 // ?? 6P
#define LORA_DIO1 22 // 20 7P
#define LORA_DIO2 23 // 2 8P
#define LORA_DIO3 19 // ?? 3P
#define LORA_DIO4 20 // ?? 4P
#define LORA_DIO5 18 // ?? 15P

#ifdef USE_SX1262
#define SX126X_CS LORA_CS
#define SX126X_DIO1 LORA_DIO1
#define SX126X_BUSY LORA_DIO2
#define SX126X_RESET LORA_RESET
#define SX126X_DIO2_AS_RF_SWITCH
// #define SX126X_DIO3_TCXO_VOLTAGE 1.8
#endif