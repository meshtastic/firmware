#define HAS_GPS 0
#undef GPS_RX_PIN
#undef GPS_TX_PIN

//#define HAS_SCREEN 0

//#define HAS_SDCARD
//#define SDCARD_USE_SPI1

#define USE_SSD1306
#define I2C_SDA 12
#define I2C_SCL 14

#define LED_PIN 46
#define LED_STATE_ON 0 // State when LED is litted

//#define BUTTON_PIN 15 // Pico OLED 1.3 User key 0 - removed User key 1 (17)

#define BUTTON_PIN 40
//#define BUTTON_PIN 0 // This is the BOOT button pad at the moment
//#define BUTTON_NEED_PULLUP

//#define USE_RF95   // RFM95/SX127x

#undef RF95_SCK
#undef RF95_MISO
#undef RF95_MOSI
#undef RF95_NSS

// WaveShare Core1262-868M OK
// https://www.waveshare.com/wiki/Core1262-868M
#define USE_SX1262

#ifdef USE_SX1262
#define RF95_MISO 39
#define RF95_SCK 21
#define RF95_MOSI 38
#define RF95_NSS 17
#define LORA_RESET 42
#define LORA_DIO1 5
#define LORA_BUSY 47
#define SX126X_CS RF95_NSS
#define SX126X_DIO1 LORA_DIO1
#define SX126X_BUSY LORA_BUSY
#define SX126X_RESET LORA_RESET
#define SX126X_E22
#endif

//#define USE_SX1280
#ifdef USE_SX1280
#define RF95_MISO 1
#define RF95_SCK 3
#define RF95_MOSI 4
#define RF95_NSS 2
#define LORA_RESET 17
#define LORA_DIO1 12
#define LORA_BUSY 47
#define SX128X_CS RF95_NSS
#define SX128X_DIO1 LORA_DIO1
#define SX128X_BUSY LORA_BUSY
#define SX128X_RESET LORA_RESET
#endif

//#define USE_EINK
/*
 * eink display pins
 */
//#define PIN_EINK_CS
//#define PIN_EINK_BUSY
//#define PIN_EINK_DC
//#define PIN_EINK_RES    (-1)
//#define PIN_EINK_SCLK   3
//#define PIN_EINK_MOSI   4