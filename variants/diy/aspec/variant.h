// aspec = Amazon Special : esp32doit-devkit-v1 + Core1262-868M
// optional OLED / GPS / BUTTON / NOTIFY

// For OLED LCD
#define I2C_SDA 21
#define I2C_SCL 22

// GPS
#define HAS_GPS 0
// #define GPS_RX_PIN
// #define GPS_TX_PIN
// #define PIN_GPS_EN
// #define GPS_POWER_TOGGLE
// #define GPS_UBLOX
// define GPS_DEBUG

// Button / Notifiy
#define BUTTON_PIN 2
#define BUTTON_NEED_PULLUP
// #define EXT_NOTIFY_OUT 12

// Core1262-868M
#define SX126X_CS 5
#define SX126X_DIO1 33
#define SX126X_BUSY 32
#define SX126X_RESET 27
#define SX126X_MAX_POWER 22
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 1.8

#define LORA_SCK 18 // clk
#define LORA_MISO 19
#define LORA_MOSI 23
#define LORA_CS 5

// supported modules list
#define USE_SX1262
