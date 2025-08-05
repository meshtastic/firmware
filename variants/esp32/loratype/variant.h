#define BUTTON_PIN 0
//#define BUTTON_PIN 39  // The middle button GPIO on the T-Beam
#define BATTERY_PIN 35 // A battery voltage measurement pin, voltage divider connected here to measure battery voltage
#define ADC_CHANNEL ADC1_GPIO35_CHANNEL
#define ADC_MULTIPLIER 2.0 // (R1 = 27k, R2 = 100k)
#define LED_PIN 33           // add status LED (compatible with core-pcb and DIY targets)

#define LORA_DIO0 RADIOLIB_NC  // a No connect on the SX1262/SX1268 module
#define LORA_RESET 16 // RST for SX1276, and for SX1262/SX1268
#define LORA_DIO1 4  // IRQ for SX1262/SX1268
#define LORA_DIO2 26  // BUSY for SX1262/SX1268
#define LORA_DIO3     // Not connected on PCB, but internally on the TTGO SX1262/SX1268, if DIO3 is high the TXCO is enabled



#undef LORA_SCK
#undef LORA_MISO
#undef LORA_MOSI
#undef LORA_CS

#define USE_SX1262

#ifdef USE_SX1262
#define LORA_SCK 18
#define LORA_MISO 19
#define LORA_MOSI 23
#define LORA_CS 5
#define LORA_RESET 16
#define LORA_DIO1 4
#define LORA_DIO2 26
#define SX126X_CS LORA_CS
#define SX126X_DIO1 LORA_DIO1
#define SX126X_BUSY LORA_DIO2
#define SX126X_RESET LORA_RESET
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 1.8
#endif

//#define SX126X_RXEN 25

#define USE_EINK
// https://docs.m5stack.com/en/core/coreink
// https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/docs/schematic/Core/coreink/coreink_sch.pdf
// #define PIN_EINK_EN -1   // N/C
#define PIN_EINK_CS   15    // EPD_CS
#define PIN_EINK_BUSY 27  // EPD_BUSY
#define PIN_EINK_DC   12  // EPD_D/C
#define PIN_EINK_RES  32  // Connected but not needed
#define PIN_EINK_SCLK 14 // EPD_SCLK
#define PIN_EINK_MOSI 13 // EPD_MOSI
