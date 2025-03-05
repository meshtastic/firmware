#define LED_PIN 15
#define LED_STATE_ON 0 // State when LED is lit

#define BUTTON_PIN 21 // This is the Program Button
#define BUTTON_NEED_PULLUP

#define I2C_SDA 22
#define I2C_SCL 23

// #define USE_RF95
#define USE_SX1262

#define LORA_MISO 20
#define LORA_SCK 19
#define LORA_MOSI 18

#ifdef USE_RF95
#define LORA_CS 17
#define LORA_RESET 1
#define LORA_DIO0 0
#define LORA_DIO1 16
#endif

#ifdef USE_SX1262
#define LORA_CS 21
#define LORA_DIO1 2
#define LORA_BUSY 0
#define LORA_RESET 5
#define SX126X_CS LORA_CS
#define SX126X_DIO1 LORA_DIO1
#define SX126X_BUSY LORA_BUSY
#define SX126X_RESET LORA_RESET
#define SX126X_RXEN 4
#define SX126X_TXEN RADIOLIB_NC
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 1.8
#endif

#define HAS_GPS 0
#undef GPS_RX_PIN
#undef GPS_TX_PIN

#define USE_XIAO_ESP32C6_EXTERNAL_ANTENNA