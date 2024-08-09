#define HAS_GPS 0
#undef GPS_RX_PIN
#undef GPS_TX_PIN

// #define HAS_SCREEN 0
// #define HAS_SDCARD
// #define SDCARD_USE_SPI1

#define USE_SSD1306

#define I2C_SDA 18 // 1 // I2C pins for this board
#define I2C_SCL 17 // 2

// #define LED_PIN 38     // This is a RGB LED not a standard LED
#define HAS_NEOPIXEL                         // Enable the use of neopixels
#define NEOPIXEL_COUNT 1                     // How many neopixels are connected
#define NEOPIXEL_DATA 38                     // gpio pin used to send data to the neopixels
#define NEOPIXEL_TYPE (NEO_GRB + NEO_KHZ800) // type of neopixels in use

#define BUTTON_PIN 0 // This is the BOOT button
#define BUTTON_NEED_PULLUP

// #define USE_RF95 // RFM95/SX127x
// #define USE_SX1262
#define USE_SX1280

#define LORA_MISO 3
#define LORA_SCK 5
#define LORA_MOSI 6
#define LORA_CS 7

#define LORA_RESET 8
#define LORA_DIO1 16

#ifdef USE_SX1262
#define LORA_BUSY 15
#define SX126X_RXEN 4
#define SX126X_TXEN 9
#endif

#ifdef USE_SX1280
#define LORA_BUSY 15
#endif

// #define USE_EINK
/*
 * eink display pins
 */
// #define PIN_EINK_CS     13
// #define PIN_EINK_BUSY   2
// #define PIN_EINK_DC     1
// #define PIN_EINK_RES    (-1)
// #define PIN_EINK_SCLK   5
// #define PIN_EINK_MOSI   6
