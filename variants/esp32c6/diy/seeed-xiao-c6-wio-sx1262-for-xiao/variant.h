// XIAO esp32c6: https://www.seeedstudio.com/Seeed-Studio-XIAO-ESP32C6-p-5884.html
//
// with
//
// Wio-SX1262-for-XIAO: https://www.seeedstudio.com/Wio-SX1262-for-XIAO-p-6379.html

#define LED_PIN 15
#define LED_STATE_ON 0 // LED is on

#define BUTTON_PIN 21 // program button
#define BUTTON_NEED_PULLUP

// XIAO wio-SX1262 shield user button
#define PIN_BUTTON1 0

// Assume we don't have a screen
#define HAS_SCREEN 0

#define USE_SX1262

#define LORA_MOSI 18
#define LORA_MISO 20
#define LORA_SCK 19
#define LORA_CS 22

#define LORA_RESET 2
#define LORA_DIO1 1

#define LORA_DIO2 23

#define LORA_BUSY 21
#define LORA_RF_SW LORA_DIO2

#define SX126X_CS LORA_CS
#define SX126X_DIO1 LORA_DIO1
#define SX126X_BUSY LORA_BUSY
#define SX126X_RESET LORA_RESET

//  DIO2 controlls an antenna switch and the TCXO voltage is controlled by DIO3
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_RXEN LORA_RF_SW
#define SX126X_TXEN RADIOLIB_NC
#define SX126X_DIO3_TCXO_VOLTAGE 1.8
