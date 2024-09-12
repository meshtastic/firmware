#define I2C_SDA 4 // I2C pins for this board
#define I2C_SCL 15

#define RESET_OLED 16 // If defined, this pin will be used to reset the display controller

#define VEXT_ENABLE 21 // active low, powers the oled display and the lora antenna boost
#define LED_PIN 2      // If defined we will blink this LED
#define BUTTON_PIN 0   // If defined, this will be used for user button presses
#define BUTTON_NEED_PULLUP
#define EXT_NOTIFY_OUT 13 // Default pin to use for Ext Notify Module.

#define USE_RF95
#define LORA_DIO0 26 // a No connect on the SX1262 module
#define LORA_RESET 14
#define LORA_DIO1 33 // Must be manually wired: https://www.thethingsnetwork.org/forum/t/big-esp32-sx127x-topic-part-3/18436
#define LORA_DIO2 32 // Not really used