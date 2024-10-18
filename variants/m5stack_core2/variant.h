#define BUTTON_NEED_PULLUP

#undef LORA_SCK
#undef LORA_MISO
#undef LORA_MOSI
#undef LORA_CS

#define LORA_SCK 18
#define LORA_MISO 38
#define LORA_MOSI 23
#define LORA_CS 33 // NSS

#define USE_RF95
#define LORA_DIO0 35 // IRQ
#define LORA_RESET 19
#define LORA_DIO1 RADIOLIB_NC // Not really used
#define LORA_DIO2 RADIOLIB_NC // Not really used

// This board has different GPS pins than all other boards
#undef GPS_RX_PIN
#undef GPS_TX_PIN
#define GPS_RX_PIN 13
#define GPS_TX_PIN 14

#define TFT_HEIGHT 240
#define TFT_WIDTH 320
#define TFT_OFFSET_X 0
#define TFT_OFFSET_Y 0
#define TFT_BUSY -1
#define TFT_OFFSET_ROTATION 0

// LCD screens are slow, so slowdown the wipe so it looks better
#define SCREEN_TRANSITION_FRAMERATE 30 // fps

// M5Stack Core2 gets a white on black display
#define TFT_MESH COLOR565(0xA0, 0xFF, 0x00) //(0x94, 0xEA, 0x67)
#define ILI9341_SPI_HOST VSPI_HOST          // VSPI_HOST or HSPI_HOST
