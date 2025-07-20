#define BUTTON_PIN 9

// Hackerboxes LoRa ESP32-C3 OLED Kit
// Uses a ESP32-C3 OLED Board and a RA-01SH (SX1262) LoRa Board

#define LED_PIN 8      // LED
#define LED_STATE_ON 1 // State when LED is lit

#define HAS_SCREEN 0
#define HAS_GPS 0
#undef GPS_RX_PIN
#undef GPS_TX_PIN

// #define USE_SSD1306_72_40
// #define I2C_SDA 5 // I2C pins for this board
// #define I2C_SCL 6 //
// #define TFT_WIDTH 72
// #define TFT_HEIGHT 40

#define USE_SX1262
#define LORA_SCK 4
#define LORA_MISO 7
#define LORA_MOSI 3
#define LORA_CS 1
#define LORA_DIO0 RADIOLIB_NC
#define LORA_RESET 0
#define LORA_DIO1 20
#define LORA_DIO2 RADIOLIB_NC
#define LORA_BUSY 10

#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_CS LORA_CS
#define SX126X_DIO1 LORA_DIO1
#define SX126X_BUSY LORA_BUSY
#define SX126X_RESET LORA_RESET
#define SX126X_MAX_POWER 22 // Max power of the RA-01SH is 22db