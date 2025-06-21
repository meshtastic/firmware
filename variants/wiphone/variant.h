#define I2C_SDA 15
#define I2C_SCL 25

#define GPIO_EXTENDER 1509
#define EXTENDER_FLAG 0x40
#define EXTENDER_PIN(x) (x + EXTENDER_FLAG)

#undef RF95_SCK
#undef RF95_MISO
#undef RF95_MOSI
#undef RF95_NSS

#define RF95_SCK 14
#define RF95_MISO 12
#define RF95_MOSI 13
#define RF95_NSS 27

#define USE_RF95
#define LORA_DIO0 38
#define LORA_RESET RADIOLIB_NC
#define LORA_DIO1 RADIOLIB_NC
#define LORA_DIO2 RADIOLIB_NC

// This board has no GPS or Screen for now
#undef GPS_RX_PIN
#undef GPS_TX_PIN
#define NO_GPS 1
#define HAS_GPS 0
#define NO_SCREEN
#define HAS_SCREEN 0

// Default SPI1 will be mapped to the display
#define ST7789_SDA 23
#define ST7789_SCK 18
#define ST7789_CS 5
#define ST7789_RS 26
// I don't have a 'wiphone' but this I think should not be defined this way (don't set TFT_BL if we don't have a hw way to control
// it)
// #define ST7789_BL -1 // EXTENDER_PIN(9)

#define ST7789_RESET -1
#define ST7789_MISO 19
#define ST7789_BUSY -1
#define ST7789_SPI_HOST SPI3_HOST
// I don't have a 'wiphone' but this I think should not be defined this way (don't set TFT_BL if we don't have a hw way to control
// it)
// #define TFT_BL -1 // EXTENDER_PIN(9)
#define SPI_FREQUENCY 40000000
#define SPI_READ_FREQUENCY 16000000
#define TFT_HEIGHT 240
#define TFT_WIDTH 320
#define TFT_OFFSET_X 0
#define TFT_OFFSET_Y 0
#define TFT_OFFSET_ROTATION 0
#define SCREEN_ROTATE
#define SCREEN_TRANSITION_FRAMERATE 5

#define I2S_MCLK_GPIO0
#define I2S_BCK_PIN 4 // rev1.3 - 4 (wp05)
#define I2S_WS_PIN 33
#define I2S_MOSI_PIN 21
#define I2S_MISO_PIN 34