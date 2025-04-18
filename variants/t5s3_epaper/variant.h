
// TODO: Display (E-Ink)
#define PIN_EINK_EN 11 // BL
#define PIN_EINK_CS 11
#define PIN_EINK_BUSY -1
#define PIN_EINK_DC 21
#define PIN_EINK_RES -1
#define PIN_EINK_SCLK 14
#define PIN_EINK_MOSI 13 // SDI

#define EPD_WIDTH 960
#define EPD_HEIGHT 540

// TODO: battery voltage measurement (I2C)
// BQ25896
// BQ27220

// #define I2C_SDA SDA
// #define I2C_SCL SCL

// optional GPS
#define GPS_DEFAULT_NOT_PRESENT 1
// #define GPS_RX_PIN 43
// #define GPS_TX_PIN 44

#define BUTTON_PIN 48
#define BUTTON_PIN_SECONDARY 0

#define USE_SX1262
#define LORA_SCK 18
#define LORA_MISO 8
#define LORA_MOSI 17
#define LORA_CS 46
#define LORA_RESET 1

#define LORA_DIO0
#define LORA_DIO1 10
#define LORA_DIO2
#define LORA_RXEN NC
#define LORA_TXEN NC

#ifdef USE_SX1262
#define SX126X_CS LORA_CS
#define SX126X_DIO1 LORA_DIO1
#define SX126X_BUSY 47
#define SX126X_RESET LORA_RESET
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 2.4
#endif
