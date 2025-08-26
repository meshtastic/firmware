#define LED_PIN 5
#define LED_INVERTED true
#define BUTTON_PIN 0

// LoRa
#define USE_RF95 // Ra-01H - SX1276
#define LORA_SCK 33
#define LORA_MISO 35
#define LORA_MOSI 32
#define LORA_CS 14
#define LORA_RESET 12
#define LORA_DIO0 36 // IRQ - connected manually
#define LORA_DIO1 34 // BUSY
#define LORA_DIO2 RADIOLIB_NC

// Display
#define HAS_SCREEN 1
#define ST7735S 1
#define ST7735_CS 2
#define ST7735_RS 4   // DC
#define ST7735_SDA 32 // MOSI
#define ST7735_SCK 33
#define ST7735_RESET -1
#define ST7735_MISO 35
#define ST7735_BUSY -1
#define ST7735_SPI_HOST VSPI_HOST
#define SPI_FREQUENCY 40000000
#define SPI_READ_FREQUENCY 16000000
#define TFT_MESH COLOR565(0xFF, 0xFF, 0xFF) // White text
#define TFT_HEIGHT 160
#define TFT_WIDTH 128
#define TFT_OFFSET_X -2
#define TFT_OFFSET_Y -1
#define TFT_INVERT false
#define TFT_BL 16
#define ST7735_BL -1
#define SCREEN_TRANSITION_FRAMERATE 5 // fps
#define DISPLAY_FORCE_SMALL_FONTS

// Touch
#define HAS_TOUCHSCREEN 1
#define USE_XPT2046 1
#define TOUCH_SPIHOST VSPI_HOST
#define TOUCH_CS 15
#define TOUCH_IRQ 39
#define TOUCH_SCK LORA_SCK
#define TOUCH_MOSI LORA_MOSI
#define TOUCH_MISO LORA_MISO
#define TOUCH_THRESHOLD_X 15
#define TOUCH_THRESHOLD_Y 100
