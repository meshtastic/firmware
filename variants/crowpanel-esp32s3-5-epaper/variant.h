#define HAS_SDCARD
#define SDCARD_USE_SPI1

// Display (E-Ink)
#define USE_EINK
#define PIN_EINK_CS 45
#define PIN_EINK_BUSY 48
#define PIN_EINK_DC 46
#define PIN_EINK_RES 47
#define PIN_EINK_SCLK 12
#define PIN_EINK_MOSI 11
#define VEXT_ENABLE 7 // e-ink power enable pin
#define VEXT_ON_VALUE HIGH

#define PIN_POWER_EN 42 // TF/SD Card Power Enable Pin

// #define BATTERY_PIN 1 // A battery voltage measurement pin, voltage divider connected here to
//  measure battery voltage ratio of voltage divider = 2.0 (assumption)
// #define ADC_MULTIPLIER 2.11 // 2.0 + 10% for correction of display undervoltage.
// #define ADC_CHANNEL ADC1_GPIO1_CHANNEL

#define I2C_SDA SDA // 21
#define I2C_SCL SCL // 15

#define GPS_DEFAULT_NOT_PRESENT 1
// #define GPS_RX_PIN 44
// #define GPS_TX_PIN 43

#define LED_PIN 41
#define BUTTON_PIN 2
#define BUTTON_NEED_PULLUP

// Buzzer - noisy ?
#define PIN_BUZZER (0 + 18)

// Wheel
//  Up         6
//  Push       5
//  Down       4
// MENU Top    2
// EXIT Bottom 1

// TTGO uses a common pinout for their SX1262 vs RF95 modules - both can be enabled and
// we will probe at runtime for RF95 and if not found then probe for SX1262
// #define USE_RF95 // RFM95/SX127x
#define USE_SX1262
// #define USE_SX1280

#define LORA_SCK 3
#define LORA_MISO 9
#define LORA_MOSI 8
#define LORA_CS 14
#define LORA_RESET 38

#define LORA_DIO1 16
#define LORA_DIO2 17

// per SX1262_Receive_Interrupt/utilities.h
#ifdef USE_SX1262
#define SX126X_CS LORA_CS
#define SX126X_DIO1 LORA_DIO1
#define SX126X_BUSY LORA_DIO2
#define SX126X_RESET LORA_RESET
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 1.8
#endif

// per SX128x_Receive_Interrupt/utilities.h
#ifdef USE_SX1280
#define SX128X_CS LORA_CS
#define SX128X_DIO1 LORA_DIO1
#define SX128X_BUSY LORA_DIO2
#define SX128X_RESET LORA_RESET
#define SX128X_RXEN 21
#define SX128X_TXEN 15
#define SX128X_MAX_POWER 3
#endif
