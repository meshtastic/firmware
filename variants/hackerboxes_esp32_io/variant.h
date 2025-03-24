#define BUTTON_PIN 0

// HACKBOX LoRa IO Kit
// Uses a ESP-32-WROOM and a RA-01SH (SX1262) LoRa Board

#define LED_PIN 2      // LED
#define LED_STATE_ON 1 // State when LED is lit

#define HAS_SCREEN 0
#define HAS_GPS 0
#undef GPS_RX_PIN
#undef GPS_TX_PIN

#define USE_SX1262
#define LORA_SCK 18
#define LORA_MISO 19
#define LORA_MOSI 23
#define LORA_CS 5
#define LORA_DIO0 RADIOLIB_NC
#define LORA_RESET 27
#define LORA_DIO1 33
#define LORA_DIO2 RADIOLIB_NC
#define LORA_BUSY 32

#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_CS LORA_CS
#define SX126X_DIO1 LORA_DIO1
#define SX126X_BUSY LORA_BUSY
#define SX126X_RESET LORA_RESET
#define SX126X_MAX_POWER 22 // Max power of the RA-01SH is 22db