#define BATTERY_PIN 15
#define ADC_CHANNEL ADC2_GPIO15_CHANNEL // ADC channel for battery voltage measurement
#define BATTERY_SENSE_SAMPLES 30
#define BAT_MEASURE_ADC_UNIT 2 // Use ADC2 for battery measurement

#define USE_SSD1306

#define BUTTON_PIN 0 // Button pin for this board
#define CANCEL_BUTTON_PIN 36
#define CANCEL_BUTTON_ACTIVE_LOW true
#define CANCEL_BUTTON_ACTIVE_PULLUP true

#define HAS_NEOPIXEL                         // If defined, we will use the neopixel library
#define NEOPIXEL_DATA 35                     // Neopixel pin for this board
#define NEOPIXEL_COUNT 1                     // Number of neopixels on this board
#define NEOPIXEL_TYPE (NEO_GRB + NEO_KHZ800) // type of neopixels in use

#define ADC_MULTIPLIER 2

#define I2C_SDA 47 // I2C pins for this board
#define I2C_SCL 48

#define USE_SX1262

#define LORA_SCK 16
#define LORA_MISO 33
#define LORA_MOSI 34
#define LORA_CS 21
#define LORA_RESET 18

#define LORA_DIO0 12 // a No connect on the SX1262 module
#define LORA_DIO1 13
#define LORA_DIO2 14 // Not really used

#define LORA_TCXO_GPIO 17

#define TCXO_OPTIONAL

#define SX126X_CS LORA_CS
#define SX126X_DIO1 LORA_DIO1
#define SX126X_BUSY LORA_DIO2
#define SX126X_RESET LORA_RESET
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 1.8
