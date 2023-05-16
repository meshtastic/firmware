#undef GPS_RX_PIN
#undef GPS_TX_PIN
#define GPS_RX_PIN 15 // per @der_bear on the forum, 36 is incorrect for this board type and 15 is a better pick
#define GPS_TX_PIN 13

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
#define LORA_DIO0 26 // a No connect on the SX1262 module
#define LORA_RESET 23
#define LORA_DIO1 33 // https://www.thethingsnetwork.org/forum/t/big-esp32-sx127x-topic-part-3/18436
#define LORA_DIO2 32 // Not really used