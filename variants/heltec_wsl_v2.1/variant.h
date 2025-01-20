#define I2C_SCL SCL
#define I2C_SDA SDA

#define LED_PIN LED

// active low, powers the Battery reader, but no lora antenna boost (?)
// #define VEXT_ENABLE Vext
// #define VEXT_ON_VALUE LOW

#define BUTTON_PIN 0

#define ADC_CTRL 21
#define ADC_CTRL_ENABLED LOW
#define BATTERY_PIN 37 // A battery voltage measurement pin, voltage divider connected here to measure battery voltage
#define ADC_CHANNEL ADC1_CHANNEL_1
// ratio of voltage divider = 3.20 (R1=100k, R2=220k)
#define ADC_MULTIPLIER 3.2

#define USE_RF95 // RFM95/SX127x

#define LORA_DIO0 26
#define LORA_RESET 14
#define LORA_DIO1 35
#define LORA_DIO2 34

#define LORA_SCK 5
#define LORA_MISO 19
#define LORA_MOSI 27
#define LORA_CS 18
