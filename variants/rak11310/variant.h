// #define RADIOLIB_CUSTOM_ARDUINO 1
// #define RADIOLIB_TONE_UNSUPPORTED 1
// #define RADIOLIB_SOFTWARE_SERIAL_UNSUPPORTED 1

#define ARDUINO_ARCH_AVR

#define LED_CONN PIN_LED2
#define LED_PIN LED_BUILTIN

#define BUTTON_PIN 9
#define BUTTON_NEED_PULLUP
// #define EXT_NOTIFY_OUT 4

#define BATTERY_PIN 26
#define BATTERY_SENSE_RESOLUTION_BITS ADC_RESOLUTION
// ratio of voltage divider = 3.0 (R17=200k, R18=100k)
#define ADC_MULTIPLIER 3.1 // 3.0 + a bit for being optimistic

#define DETECTION_SENSOR_EN 28

#define USE_SX1262

#undef LORA_SCK
#undef LORA_MISO
#undef LORA_MOSI
#undef LORA_CS

// RAK BSP somehow uses SPI1 instead of SPI0
#define HW_SPI1_DEVICE
#define LORA_SCK PIN_SPI0_SCK
#define LORA_MOSI PIN_SPI0_MOSI
#define LORA_MISO PIN_SPI0_MISO
#define LORA_CS PIN_SPI0_SS

#define LORA_DIO0 RADIOLIB_NC
#define LORA_RESET 14
#define LORA_DIO1 29
#define LORA_DIO2 15
#define LORA_DIO3 RADIOLIB_NC

#ifdef USE_SX1262
#define SX126X_CS LORA_CS
#define SX126X_DIO1 LORA_DIO1
#define SX126X_BUSY LORA_DIO2
#define SX126X_RESET LORA_RESET
#define SX126X_POWER_EN 25
// DIO2 controlls an antenna switch and the TCXO voltage is controlled by DIO3
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 1.8
#endif