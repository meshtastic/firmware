#define I2C_SDA 21 //-1
#define I2C_SCL 22 //-1

//#define LED_PIN 10

#include "pcf8563.h"
// PCF8563 RTC Module
#define PCF8563_RTC 0x51

#define BUTTON_NEED_PULLUP
#define BUTTON_PIN 5

//Wheel
// Down 37
// Push 38
// Up 39

// Top Physical Button 5

#undef RF95_SCK
#undef RF95_MISO
#undef RF95_MOSI
#undef RF95_NSS
#define USE_RF95

#define RF95_SCK  18 //13
#define RF95_MISO 34 //26
#define RF95_MOSI 23 //25 
#define RF95_NSS 14 
#define LORA_DIO0 25 //32 now moved from ext port
#define LORA_RESET 26 //33 now moved from ext port
#define LORA_DIO1 RADIOLIB_NC
#define LORA_DIO2 RADIOLIB_NC

// This board has no GPS for now
#undef GPS_RX_PIN
#undef GPS_TX_PIN

#define USE_EINK
//https://docs.m5stack.com/en/core/coreink
//https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/docs/schematic/Core/coreink/coreink_sch.pdf
#define PIN_EINK_EN    -1
#define PIN_EINK_CS    9          // EPD_CS
#define PIN_EINK_BUSY  4          // EPD_BUSY
#define PIN_EINK_DC    15          // EPD_D/C
#define PIN_EINK_RES   -1          // Connected to GPIO0 but no needed !!!! maybe causing issue ?
#define PIN_EINK_SCLK  18           // EPD_SCLK
#define PIN_EINK_MOSI  23          // EPD_MOSI

#define HAS_RTC 1
