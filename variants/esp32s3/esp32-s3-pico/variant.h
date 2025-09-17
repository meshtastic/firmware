/*

*/
#define HAS_GPS 0
#undef GPS_RX_PIN
#undef GPS_TX_PIN

#define EXT_NOTIFY_OUT 22
#define BUTTON_PIN 0 // 17

// #define LED_PIN PIN_LED
// Board has RGB LED 21
#define HAS_NEOPIXEL                         // Enable the use of neopixels
#define NEOPIXEL_COUNT 1                     // How many neopixels are connected
#define NEOPIXEL_DATA 21                     // gpio pin used to send data to the neopixels
#define NEOPIXEL_TYPE (NEO_GRB + NEO_KHZ800) // type of neopixels in use

// The usbPower state is revered ?
// DEBUG | ??:??:?? 365 [Power] Battery: usbPower=0, isCharging=0, batMv=4116, batPct=90
// DEBUG | ??:??:?? 385 [Power] Battery: usbPower=1, isCharging=1, batMv=4243, batPct=0

// https://www.waveshare.com/img/devkit/ESP32-S3-Pico/ESP32-S3-Pico-details-inter-1.jpg
// digram is incorrect labeled as battery pin is getting readings on GPIO7_CH1?
#define BATTERY_PIN 7
#define ADC_CHANNEL ADC1_GPIO7_CHANNEL
// #define ADC_CHANNEL ADC1_GPIO6_CHANNEL
//   ratio of voltage divider = 3.0 (R17=200k, R18=100k)
#define ADC_MULTIPLIER 3.1 // 3.0 + a bit for being optimistic

#define I2C_SDA 15
#define I2C_SCL 16

// Enable secondary bus for external periherals
// https://www.waveshare.com/wiki/Pico-OLED-1.3
// #define USE_SH1107_128_64
// Not working
#define I2C_SDA1 17
#define I2C_SCL1 18

#define BUTTON_PIN 0 // This is the BOOT button
#define BUTTON_NEED_PULLUP

// #define USE_RF95 // RFM95/SX127x
#define USE_SX1262
// #define USE_SX1280

#define LORA_MISO 37
#define LORA_SCK 35
#define LORA_MOSI 36
#define LORA_CS 14

#define LORA_RESET 40
#define LORA_DIO1 4
#define LORA_DIO2 13

#ifdef USE_SX1262
#define SX126X_CS LORA_CS
#define SX126X_DIO1 LORA_DIO1
#define SX126X_BUSY LORA_DIO2
#define SX126X_RESET LORA_RESET
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 1.8
#endif

#ifdef USE_SX1280
#define SX128X_CS LORA_CS
#define SX128X_DIO1 LORA_DIO1
#define SX128X_BUSY 9
#define SX128X_RESET LORA_RESET
#endif

#define USE_EINK
/*
 * eink display pins
 */
#define PIN_EINK_CS 34
#define PIN_EINK_BUSY 38
#define PIN_EINK_DC 33
#define PIN_EINK_RES 42 // 37 //(-1) // cant be MISO Waveshare ??)
#define PIN_EINK_SCLK 35
#define PIN_EINK_MOSI 36