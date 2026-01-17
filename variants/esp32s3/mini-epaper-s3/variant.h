// Display (E-Ink)

#define PIN_EINK_CS 13
#define PIN_EINK_BUSY 10
#define PIN_EINK_RES 11
#define PIN_EINK_SCLK 14
#define PIN_EINK_MOSI 15
#define PIN_EINK_DC 12
#define PIN_EINK_EN 42

#define SPI_INTERFACES_COUNT 2
#define PIN_SPI1_MISO -1
#define PIN_SPI1_MOSI PIN_EINK_MOSI
#define PIN_SPI1_SCK PIN_EINK_SCLK

#define I2C_SDA SDA
#define I2C_SCL SCL

#define BATTERY_PIN 2 // A battery voltage measurement pin, voltage divider connected here to
// measure battery voltage ratio of voltage divider = 2.0 (assumption)
#define ADC_MULTIPLIER 2.11 // 2.0 + 10% for correction of display undervoltage.
#define ADC_CHANNEL ADC1_GPIO2_CHANNEL

#define HAS_GPS 0
#undef GPS_RX_PIN
#undef GPS_TX_PIN

#define BUTTON_PIN 3
#define BUTTON_NEED_PULLUP
#define ALT_BUTTON_PIN 4
#define ALT_BUTTON_ACTIVE_LOW true
#define ALT_BUTTON_ACTIVE_PULLUP true
#define PIN_BUTTON3 0

// #define HAS_SDCARD 1
// #define SDCARD_USE_SOFT_SPI

// PCF85063 RTC Module
#define PCF85063_RTC 0x51
#define HAS_RTC 1

#define USE_SX1262
#define LORA_DIO1 5
#define LORA_SCK 8
#define LORA_MISO 6
#define LORA_MOSI 17
#define LORA_CS 7 // CS not connected; IO7 is free
#define LORA_RESET 21

#ifdef USE_SX1262
#define SX126X_CS LORA_CS
#define SX126X_DIO1 5
#define SX126X_BUSY 16
#define SX126X_RESET LORA_RESET
#define SX126X_DIO3_TCXO_VOLTAGE 1.8
#endif
