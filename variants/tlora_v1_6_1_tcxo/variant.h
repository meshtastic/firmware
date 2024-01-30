#define BATTERY_PIN 35
#define ADC_CHANNEL ADC1_GPIO35_CHANNEL
#define BATTERY_SENSE_SAMPLES 30

// ratio of voltage divider = 2.0 (R42=100k, R43=100k)
#define ADC_MULTIPLIER 2

#define I2C_SDA 21 // I2C pins for this board
#define I2C_SCL 22

#define LED_PIN 25    // If defined we will blink this LED
#define BUTTON_PIN 12 // If defined, this will be used for user button presses,

#define BUTTON_NEED_PULLUP

#define USE_RF95
#define LORA_DIO0 26
#define LORA_RESET 23
#define LORA_DIO1 RADIOLIB_NC // No connect on the SX1276 module
#define LORA_DIO2 32
#define RF95_TCXO 33            //TCXO Enable
