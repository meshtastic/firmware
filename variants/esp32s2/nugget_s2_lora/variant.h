#define I2C_SDA 34 // I2C pins for this board
#define I2C_SCL 36

#define LED_PIN 15 // If defined we will blink this LED

#define HAS_NEOPIXEL                         // Enable the use of neopixels
#define NEOPIXEL_COUNT 3                     // How many neopixels are connected
#define NEOPIXEL_DATA 12                     // gpio pin used to send data to the neopixels
#define NEOPIXEL_TYPE (NEO_GRB + NEO_KHZ800) // type of neopixels in use

#define BUTTON_PIN 0 // If defined, this will be used for user button presses
#define BUTTON_NEED_PULLUP

#define USE_RF95
#define LORA_SCK 6
#define LORA_MISO 8
#define LORA_MOSI 10
#define LORA_CS 13
#define LORA_DIO0 16
#define LORA_RESET 5

#define LORA_DIO1 RADIOLIB_NC
#define LORA_DIO2 RADIOLIB_NC