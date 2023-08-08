#undef GPS_RX_PIN
#undef GPS_TX_PIN

#define BUTTON_PIN 0

#define PIN_BUZZER 43

#define HAS_GPS 0
#define HAS_WIRE 0

#define USE_RF95 // RFM95/SX127x

#define RF95_SCK SCK   // 21
#define RF95_MISO MISO // 39
#define RF95_MOSI MOSI // 38
#define RF95_NSS SS    // 40
#define LORA_RESET RADIOLIB_NC

// per SX1276_Receive_Interrupt/utilities.h
#define LORA_DIO0 10
#define LORA_DIO1 RADIOLIB_NC
#define LORA_DIO2 RADIOLIB_NC

// Default SPI1 will be mapped to the display
#define ST7789_SDA 4
#define ST7789_SCK 3
#define ST7789_CS 6
#define ST7789_RS 1
#define ST7789_BL 5

#define ST7789_RESET -1
#define ST7789_MISO -1
#define ST7789_BUSY -1
#define ST7789_SPI_HOST SPI3_HOST
#define ST7789_BACKLIGHT_EN 5
#define SPI_FREQUENCY 40000000
#define SPI_READ_FREQUENCY 16000000
#define TFT_HEIGHT 320
#define TFT_WIDTH 240
#define TFT_OFFSET_X 0
#define TFT_OFFSET_Y 0
#define TFT_OFFSET_ROTATION 0
#define SCREEN_ROTATE
#define SCREEN_TRANSITION_FRAMERATE 5

#define INPUTBROKER_MATRIX_TYPE 1

#define KEYS_COLS              \
    {                          \
        44, 47, 17, 15, 13, 41 \
    }
#define KEYS_ROWS             \
    {                         \
        12, 16, 42, 18, 14, 7 \
    }
