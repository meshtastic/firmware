#define I2C_SDA 1
#define I2C_SCL 0

#define BUTTON_PIN 3 // M5Stack STAMP C3 built in button
#define BUTTON_NEED_PULLUP

// #define HAS_SCREEN 0
#define HAS_GPS 0
#undef GPS_RX_PIN
#undef GPS_TX_PIN

#undef LORA_SCK
#undef LORA_MISO
#undef LORA_MOSI
#undef LORA_CS

// Adafruit RFM95W OK
// https://www.adafruit.com/product/3072
#define USE_RF95
#define LORA_SCK 4
#define LORA_MISO 5
#define LORA_MOSI 6
#define LORA_CS 7
#define LORA_DIO0 10
#define LORA_RESET 8
#define LORA_DIO1 RADIOLIB_NC
#define LORA_DIO2 RADIOLIB_NC

// WaveShare Core1262-868M OK
// https://www.waveshare.com/wiki/Core1262-868M
// #define USE_SX1262
// #define LORA_SCK 4
// #define LORA_MISO 5
// #define LORA_MOSI 6
// #define LORA_CS 7
// #define LORA_DIO0 RADIOLIB_NC
// #define LORA_RESET 8
// #define LORA_DIO1 10
// #define LORA_DIO2 RADIOLIB_NC
// #define LORA_BUSY 18
// #define SX126X_CS LORA_CS
// #define SX126X_DIO1 LORA_DIO1
// #define SX126X_BUSY LORA_BUSY
// #define SX126X_RESET LORA_RESET
// #define SX126X_DIO2_AS_RF_SWITCH
// #define SX126X_DIO3_TCXO_VOLTAGE 1.8

// SX128X 2.4 Ghz LoRa module Not OK - RadioLib issue ? still to confirm
// #define USE_SX1280
// #define LORA_SCK 4
// #define LORA_MISO 5
// #define LORA_MOSI 6
// #define LORA_CS 7
// #define LORA_DIO0 -1
// #define LORA_DIO1 10
// #define LORA_DIO2 21
// #define LORA_RESET 8
// #define LORA_BUSY 1
// #define SX128X_CS LORA_CS
// #define SX128X_DIO1 LORA_DIO1
// #define SX128X_BUSY LORA_BUSY
// #define SX128X_RESET LORA_RESET
// #define SX128X_MAX_POWER 10

// Not yet tested
// #define USE_EINK
// #define PIN_EINK_EN   -1  // N/C
// #define PIN_EINK_CS   9   // EPD_CS
// #define PIN_EINK_BUSY 18  // EPD_BUSY
// #define PIN_EINK_DC   19  // EPD_D/C
// #define PIN_EINK_RES  -1  // Connected but not needed
// #define PIN_EINK_SCLK 4   // EPD_SCLK
// #define PIN_EINK_MOSI 6   // EPD_MOSI
