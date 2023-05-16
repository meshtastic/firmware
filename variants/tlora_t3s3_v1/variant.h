#undef GPS_RX_PIN
#undef GPS_TX_PIN

#define HAS_SDCARD
#define SDCARD_USE_SPI1

#define USE_SSD1306

#define BATTERY_PIN 1 // A battery voltage measurement pin, voltage divider connected here to measure battery voltage
// ratio of voltage divider = 2.0 (R42=100k, R43=100k)
#define ADC_MULTIPLIER 2.11 // 2.0 + 10% for correction of display undervoltage.
#define ADC_CHANNEL ADC1_GPIO1_CHANNEL

#define I2C_SDA 18 // I2C pins for this board
#define I2C_SCL 17

#define I2C_SDA1 43
#define I2C_SCL1 44

#define LED_PIN 37   // If defined we will blink this LED
#define BUTTON_PIN 0 // If defined, this will be used for user button presses,

#define BUTTON_NEED_PULLUP

// TTGO uses a common pinout for their SX1262 vs RF95 modules - both can be enabled and we will probe at runtime for RF95 and if
// not found then probe for SX1262
#define USE_RF95 // RFM95/SX127x
#define USE_SX1262
#define USE_SX1280

#define RF95_SCK 5
#define RF95_MISO 3
#define RF95_MOSI 6
#define RF95_NSS 7
#define LORA_RESET 8

// per SX1276_Receive_Interrupt/utilities.h
#define LORA_DIO0 9
#define LORA_DIO1 33 // TCXO_EN ?
#define LORA_DIO2 34
#define LORA_RXEN 21
#define LORA_TXEN 10

// per SX1262_Receive_Interrupt/utilities.h
#ifdef USE_SX1262
#define SX126X_CS RF95_NSS
#define SX126X_DIO1 33
#define SX126X_BUSY 34
#define SX126X_RESET LORA_RESET
#define SX126X_E22
#endif

// per SX128x_Receive_Interrupt/utilities.h
#ifdef USE_SX1280
#define SX128X_CS RF95_NSS
#define SX128X_DIO1 9
#define SX128X_DIO2 33
#define SX128X_DIO3 34
#define SX128X_BUSY 36
#define SX128X_RESET LORA_RESET
#define SX128X_RXEN 21
#define SX128X_TXEN 10
#define SX128X_MAX_POWER 3
#endif