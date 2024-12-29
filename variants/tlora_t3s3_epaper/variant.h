#define HAS_SDCARD
#define SDCARD_USE_SPI1

// Display (E-Ink)
#define USE_EINK
#define PIN_EINK_CS 15
#define PIN_EINK_BUSY 48
#define PIN_EINK_DC 16
#define PIN_EINK_RES 47
#define PIN_EINK_SCLK 14
#define PIN_EINK_MOSI 11

#define BATTERY_PIN 1 // A battery voltage measurement pin, voltage divider connected here to
// measure battery voltage ratio of voltage divider = 2.0 (assumption)
#define ADC_MULTIPLIER 2.11 // 2.0 + 10% for correction of display undervoltage.
#define ADC_CHANNEL ADC1_GPIO1_CHANNEL

#define I2C_SDA SDA
#define I2C_SCL SCL

// external qwiic connector
#define GPS_DEFAULT_NOT_PRESENT 1
#define GPS_RX_PIN 44
#define GPS_TX_PIN 43

#define LED_PIN 37
#define BUTTON_PIN 0
#define BUTTON_NEED_PULLUP

// TTGO uses a common pinout for their SX1262 vs RF95 modules - both can be enabled and
// we will probe at runtime for RF95 and if not found then probe for SX1262
#define USE_RF95 // RFM95/SX127x
#define USE_SX1262
#define USE_SX1280

#define LORA_SCK 5
#define LORA_MISO 3
#define LORA_MOSI 6
#define LORA_CS 7
#define LORA_RESET 8

// per SX1276_Receive_Interrupt/utilities.h
#define LORA_DIO0 9
#define LORA_DIO1 33 // TCXO_EN ?
#define LORA_DIO2 34
#define LORA_RXEN 21
#define LORA_TXEN 10

// per SX1262_Receive_Interrupt/utilities.h
#ifdef USE_SX1262
#define SX126X_CS LORA_CS
#define SX126X_DIO1 33
#define SX126X_BUSY 34
#define SX126X_RESET LORA_RESET
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 1.8
#endif

// per SX128x_Receive_Interrupt/utilities.h
#ifdef USE_SX1280
#define SX128X_CS LORA_CS
#define SX128X_DIO1 9
#define SX128X_DIO2 33
#define SX128X_DIO3 34
#define SX128X_BUSY 36
#define SX128X_RESET LORA_RESET
#define SX128X_RXEN 21
#define SX128X_TXEN 10
#define SX128X_MAX_POWER 3
#endif
