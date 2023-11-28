// https://betafpv.com/products/elrs-nano-tx-module

// no screen
#define HAS_SCREEN 0

// NO GPS
#undef GPS_RX_PIN
#undef GPS_TX_PIN

#define USE_RF95

#define SPI_SCK 18
#define SPI_MISO 19
#define SPI_MOSI 23
#define SPI_NSS 5

#define LORA_DIO0 4
#define LORA_RESET 14
#define LORA_DIO1 2
#define LORA_DIO2
#define LORA_DIO3

#define LED_PIN 16 // green - blue is at 17

#define BUTTON_PIN 25
#define BUTTON_NEED_PULLUP

#undef EXT_NOTIFY_OUT
