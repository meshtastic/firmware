#define HAS_GPS 0
#undef GPS_RX_PIN
#undef GPS_TX_PIN

#define USE_ST7789

#define LED_PIN 48 // If defined we will blink this LED

#define BUTTON_PIN 0 // If defined, this will be used for user button presses,

#define BUTTON_NEED_PULLUP

#define USE_RF95 // RFM95/SX127x

#define RF95_SCK 10
#define RF95_MISO 12
#define RF95_MOSI 11
#define RF95_NSS 13
#define LORA_RESET RADIOLIB_NC

// per SX1276_Receive_Interrupt/utilities.h
#define LORA_DIO0 28
#define LORA_DIO1 RADIOLIB_NC
#define LORA_DIO2 RADIOLIB_NC