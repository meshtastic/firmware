#define BUTTON_PIN 9

// LED pin on HT-DEV-ESP_V2 and HT-DEV-ESP_V3
// https://resource.heltec.cn/download/HT-CT62/HT-CT62_Reference_Design.pdf
// https://resource.heltec.cn/download/HT-DEV-ESP/HT-DEV-ESP_V3_Sch.pdf
#define LED_PIN 2 // LED
#define LED_INVERTED 0

#define HAS_SCREEN 0
#define HAS_GPS 0
#undef GPS_RX_PIN
#undef GPS_TX_PIN

#define USE_SX1262
#define LORA_SCK 10
#define LORA_MISO 6
#define LORA_MOSI 7
#define LORA_CS 8
#define LORA_DIO0 RADIOLIB_NC
#define LORA_RESET 5
#define LORA_DIO1 3
#define LORA_DIO2 RADIOLIB_NC
#define LORA_BUSY 4
#define SX126X_CS LORA_CS
#define SX126X_DIO1 LORA_DIO1
#define SX126X_BUSY LORA_BUSY
#define SX126X_RESET LORA_RESET
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 1.8
