// #define BUTTON_NEED_PULLUP // if set we need to turn on the internal CPU pullup during sleep

#define I2C_SDA 8
#define I2C_SCL 9

#define BUTTON_PIN 0

#define USE_RF95
#undef RF95_SCK
#define RF95_SCK 4
#undef RF95_MISO
#define RF95_MISO 5
#undef RF95_MOSI
#define RF95_MOSI 6
#undef RF95_NSS
#define RF95_NSS 7

#define LORA_DIO0 10 // a No connect on the SX1262 module
#define LORA_DIO1 3  // a No connect on the SX1262 module
#define LORA_RESET 2

#undef GPS_RX_PIN
#undef GPS_TX_PIN

#define HAS_SCREEN 0
#define HAS_GPS 0
