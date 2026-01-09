// Seeed Xiao ESP32c3 variant
//
// This MCU is not bundled with a LoRa radio.
// Instead, it includes definitions for the radio included with the "ESP32-S3 LoRa" kit,
// and the radio included with the "Xiao nRF52840 LoRa" kit.

// #define BUTTON_PIN 9 // TODO (this varies based on the radio 'hat')
// #define LED_PIN 2    // TODO
// #define LED_STATE_ON 1

#define HAS_SCREEN 0
#define HAS_GPS 0
#undef GPS_RX_PIN
#undef GPS_TX_PIN

#define USE_SX1262

// https://files.seeedstudio.com/wiki/XIAO_ESP32S3_for_Meshtastic_LoRa/10.png
#ifdef XIAO_C3_loraS3
#define LORA_MOSI 10
#define LORA_MISO 9
#define LORA_SCK 8
#define LORA_DIO1 2
#define LORA_BUSY 3
#define LORA_RESET 4
#define LORA_CS 5
#define LORA_DIO2 6
#endif

// https://media-cdn.seeedstudio.com/media/wysiwyg/upload/image_Wio-SX1262_-1.png
#ifdef XIAO_C3_loraNRF52
#define LORA_MOSI 10
#define LORA_MISO 9
#define LORA_SCK 8
#define LORA_DIO1 3
#define LORA_BUSY 5
#define LORA_RESET 4
#define LORA_CS 6
#define LORA_DIO2 7
#endif

#define LORA_DIO0 RADIOLIB_NC
#define SX126X_CS LORA_CS
#define SX126X_DIO1 LORA_DIO1
#define SX126X_BUSY LORA_BUSY
#define SX126X_RESET LORA_RESET
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_RXEN LORA_DIO2
#define SX126X_TXEN RADIOLIB_NC
#define SX126X_DIO3_TCXO_VOLTAGE 1.8
