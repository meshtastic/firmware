// #define RADIOLIB_CUSTOM_ARDUINO 1
// #define RADIOLIB_TONE_UNSUPPORTED 1
// #define RADIOLIB_SOFTWARE_SERIAL_UNSUPPORTED 1

#define ARDUINO_ARCH_AVR

// default I2C pins:
// SDA = 4
// SCL = 5

// Recommended pins for SerialModule:
// txd = 8
// rxd = 9

#define EXT_NOTIFY_OUT 5
#define BUTTON_PIN 4

// Disable default LED - GPIO25 is used for RGB Blue
#undef LED_PIN

// RGB LED pins (active low - common anode)
#define RGBLED_RED 23
#define RGBLED_GREEN 24
#define RGBLED_BLUE 25
#define RGBLED_CA   // Common anode - high = off, low = on

// #define I2C_SDA1 26
// #define I2C_SCL1 27
// #undef HAS_SCREEN
#define I2C_SDA1 6
#define I2C_SCL1 7

// #define BATTERY_PIN 26
// ratio of voltage divider = 3.0 (R17=200k, R18=100k)
// #define ADC_MULTIPLIER 3.1 // 3.0 + a bit for being optimistic
// #define BATTERY_SENSE_RESOLUTION_BITS ADC_RESOLUTION

#define USE_SX1262

#undef LORA_SCK
#undef LORA_MISO
#undef LORA_MOSI
#undef LORA_CS

#define LORA_SCK 2
#define LORA_MOSI 3
#define LORA_MISO 0
#define LORA_CS 1

#define LORA_DIO0 RADIOLIB_NC
#define LORA_RESET 28
#define LORA_BUSY 29
#define LORA_DIO1 26
#define LORA_DIO2 27
#define LORA_DIO3 RADIOLIB_NC

#ifdef USE_SX1262
#define SX126X_CS LORA_CS
#define SX126X_DIO1 LORA_DIO1
#define SX126X_DIO2 LORA_DIO2
#define SX126X_BUSY LORA_BUSY
#define SX126X_RESET LORA_RESET
#define SX126X_DIO2_AS_RF_SWITCH
// #define SX126X_DIO3_TCXO_VOLTAGE 1.8
#define SX126X_DIO3_TCXO_VOLTAGE 1.8
#endif
