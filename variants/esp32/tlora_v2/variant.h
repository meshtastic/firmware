#define BATTERY_PIN 35 // A battery voltage measurement pin, voltage divider connected here to measure battery voltage
#define ADC_CHANNEL ADC1_GPIO35_CHANNEL

#define I2C_SDA 21 // I2C pins for this board
#define I2C_SCL 22

#define VEXT_ENABLE 21 // active low, powers the oled display and the lora antenna boost
#define LED_PIN 25     // If defined we will blink this LED
#define BUTTON_PIN                                                                                                               \
    0 // If defined, this will be used for user button presses, if your board doesn't have a physical switch, you can wire one
      // between this pin and ground
#define BUTTON_NEED_PULLUP

#define USE_RF95
#define LORA_DIO0 26 // a No connect on the SX1262 module
#define LORA_RESET 14
#define LORA_DIO1 33 // Must be manually wired: https://www.thethingsnetwork.org/forum/t/big-esp32-sx127x-topic-part-3/18436
#define LORA_DIO2 32 // Not really used