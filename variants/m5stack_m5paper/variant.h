#include "pcf8563.h"

//#define LED_PIN -1

#define I2C_SDA 21 
#define I2C_SCL 22 

// PCF8563 RTC Module
#define HAS_RTC 1
#define PCF8563_RTC 0x51

#define HAS_TEMP_SENSOR 1
#define SHT3x_ADDR 0x44

// #define HAS_TOUCHSCREEN 1
// #define SCREEN_TOUCH_INT 36
// #define TOUCH_I2C_PORT 0
// #define TOUCH_SLAVE_ADDRESS 0x5D // GT911

#define BATTERY_PIN 35
#define ADC_CHANNEL ADC1_GPIO35_CHANNEL
#define ADC_ATTENUATION ADC_ATTEN_DB_11
#define ADC_WIDTH ADC_WIDTH_BIT_12
#define ADC_MULTIPLIER 2
#define DEFAULT_VREF 3600

//Wheel
// Down 37
// Push 38
// Up 39

//#define BUTTON_NEED_PULLUP
#define BUTTON_PIN 38

// #undef RF95_SCK
// #undef RF95_MISO
// #undef RF95_MOSI
// #undef RF95_NSS
// #define USE_RF95

// #define RF95_SCK  18 //13
// #define RF95_MISO 34 //26
// #define RF95_MOSI 23 //25 
// #define RF95_NSS 14 
// #define LORA_DIO0 25 
// #define LORA_RESET 26 
// #define LORA_DIO1 RADIOLIB_NC
// #define LORA_DIO2 RADIOLIB_NC

#define NO_GPS
// This board has no GPS but there's an UART connector
// #define GPS_RX_PIN 19
// #define GPS_TX_PIN 18

#define HAS_SDCARD 1
#define SPI_MOSI (12)
#define SPI_SCK (14)
#define SPI_MISO (13)
#define SPI_CS (4)
#define SDCARD_CS SPI_CS

#define HAS_EINK 1
#define USE_EINK

#define PIN_EINK_MOSI  SPI_MOSI          // EPD_MOSI
#define PIN_EINK_MISO  SPI_MISO          // EPD_MISO
#define PIN_EINK_SCLK  SPI_SCK          // EPD_SCLK
#define PIN_EINK_CS    15          // EPD_CS
#define PIN_EINK_BUSY  27          // EPD_BUSY

#define PIN_EINK_EN    -1
#define PIN_EINK_DC    -1          // EPD_D/C
#define PIN_EINK_RES   -1          // EPD RES

#define M5EPD_MAIN_PWR_PIN   2
#define M5EPD_EXT_PWR_EN_PIN 5
#define M5EPD_EPD_PWR_EN_PIN 23
