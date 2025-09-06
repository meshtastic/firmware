// Primary I2C Bus includes PCF8563 RTC Module
#define I2C_SDA 21
#define I2C_SCL 22

#define HAS_GPS 1
#undef GPS_RX_PIN
#undef GPS_TX_PIN
// Use Secondary I2C Bus as GPS Serial
#define GPS_RX_PIN 33
// #define GPS_TX_PIN 32 (now used by SX1262 BUSY as GPS works with just RX)

// Green LED
#define LED_STATE_ON 1 // State when LED is lit
#define LED_PIN 10

#include "pcf8563.h"
// PCF8563 RTC Module
#define PCF8563_RTC 0x51
#define HAS_RTC 1

// Wheel
//  Down 37
//  Push 38
//  Up 39
//  Top Physical Button 5

#define BUTTON_NEED_PULLUP
#define BUTTON_PIN 5

// BUZZER
#define PIN_BUZZER 2

#undef LORA_SCK
#undef LORA_MISO
#undef LORA_MOSI
#undef LORA_CS

#define USE_RF95
// #define USE_SX1262
// #define USE_SX1280

#ifdef USE_RF95
#define LORA_SCK 18
#define LORA_MISO 34
#define LORA_MOSI 23
#define LORA_CS 14
#define LORA_DIO0 25
#define LORA_RESET 26
#define LORA_DIO1 RADIOLIB_NC
#define LORA_DIO2 RADIOLIB_NC
#endif

// https://www.waveshare.com/core1262-868m.htm
#ifdef USE_SX1262
#define LORA_SCK 18
#define LORA_MISO 34
#define LORA_MOSI 23
#define LORA_CS 14
#define LORA_RESET 26
#define LORA_DIO1 25
#define LORA_DIO2 32 // 33 // (13 not working)  //BUSY pin on SX1262
#define SX126X_CS LORA_CS
#define SX126X_DIO1 LORA_DIO1
#define SX126X_BUSY LORA_DIO2
#define SX126X_RESET LORA_RESET
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 1.8
#endif

#ifdef USE_SX1280
#define LORA_SCK 18
#define LORA_MISO 34
#define LORA_MOSI 23
#define LORA_CS 14
#define LORA_RESET 26
#define LORA_DIO1 25
#define LORA_DIO2 13
#define SX128X_CS LORA_CS
#define SX128X_DIO1 LORA_DIO1
#define SX128X_BUSY LORA_DIO2
#define SX128X_RESET LORA_RESET
#define SX128X_MAX_POWER 13 // 10
#endif

#define USE_EINK
// https://docs.m5stack.com/en/core/coreink
// https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/docs/schematic/Core/coreink/coreink_sch.pdf
#define PIN_EINK_EN -1   // N/C
#define PIN_EINK_CS 9    // EPD_CS
#define PIN_EINK_BUSY 4  // EPD_BUSY
#define PIN_EINK_DC 15   // EPD_D/C
#define PIN_EINK_RES -1  // Connected but not needed
#define PIN_EINK_SCLK 18 // EPD_SCLK
#define PIN_EINK_MOSI 23 // EPD_MOSI

#define BATTERY_PIN 35
#define ADC_CHANNEL ADC1_GPIO35_CHANNEL
// https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/docs/schematic/Core/m5paper/M5_PAPER_SCH.pdf
// https://github.com/m5stack/M5Core-Ink/blob/master/examples/Basics/FactoryTest/FactoryTest.ino#L58
//  VBAT
//   |
//  R83 (3K)
//   +
//  R86 (11K)
//   |
//  GND
// https://github.com/m5stack/M5Core-Ink/blob/master/examples/Basics/FactoryTest/FactoryTest.ino#L58
#define ADC_MULTIPLIER 5
// https://embeddedexplorer.com/esp32-adc-esp-idf-tutorial/