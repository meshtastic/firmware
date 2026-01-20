#define BATTERY_PIN 35 // A battery voltage measurement pin, voltage divider connected here to measure battery voltage
// ratio of voltage divider = 2.0 (R42=100k, R43=100k)
#define ADC_MULTIPLIER 2.11 // 2.0 + 10% for correction of display undervoltage.
#define ADC_CHANNEL ADC1_GPIO35_CHANNEL

#define I2C_SDA 21 // I2C pins for this board
#define I2C_SCL 22

#define LED_PIN 25    // If defined we will blink this LED
#define BUTTON_PIN 12 // If defined, this will be used for user button presses,

#define BUTTON_NEED_PULLUP

#define USE_SX1280
#define LORA_RESET 23

#define SX128X_CS 18
#define SX128X_DIO1 26
#define SX128X_BUSY 32
#define SX128X_RESET LORA_RESET