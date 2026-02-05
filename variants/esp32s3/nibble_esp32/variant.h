#define I2C_SDA 11 // I2C pins for this board
#define I2C_SCL 10

#define LED_PIN 1 // If defined we will blink this LED

#define BUTTON_PIN 0 // If defined, this will be used for user button presses
#define BUTTON_NEED_PULLUP

#define USE_RF95
#define LORA_SCK 6
#define LORA_MISO 7
#define LORA_MOSI 8
#define LORA_CS 9
#define LORA_DIO0 5 // a No connect on the SX1262 module
#define LORA_RESET 4

#define LORA_DIO1 RADIOLIB_NC
#define LORA_DIO2 RADIOLIB_NC