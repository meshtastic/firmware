// CatWAN USB Stick - Electronic Cats
// RP2040 + RFM95W (SX1276)
//
// Pin mapping:
//   SPI0: MISO=GPIO16, CS=GPIO17, SCK=GPIO18, MOSI=GPIO19
//   Radio: RST=GPIO21, DIO0=GPIO10, DIO1=GPIO11, DIO2=GPIO12
//   LED: GPIO25

#define HAS_SCREEN 0
#define HAS_GPS 0
#undef GPS_RX_PIN
#undef GPS_TX_PIN

#define LED_PIN 25

#define USE_RF95 // RFM95W / SX1276

#undef LORA_SCK
#undef LORA_MISO
#undef LORA_MOSI
#undef LORA_CS

#define LORA_SCK 18
#define LORA_MISO 16
#define LORA_MOSI 19
#define LORA_CS 17

#define LORA_RESET 21
#define LORA_DIO0 10
#define LORA_DIO1 11
#define LORA_DIO2 12
