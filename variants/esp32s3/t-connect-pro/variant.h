// LilyGo T-Connect-Pro (ESP32-S3R8). The LoRa module, the ST7796 LCD and the W5500 all hang off
// one SPI bus (SCK 12 / MISO 13 / MOSI 11), so every peripheral has to stay on SPI2_HOST - the
// host the Arduino `SPI` object uses on the S3 - and share it through transactions.

#define I2C_SDA 39
#define I2C_SCL 40

#define BUTTON_PIN 0 // BOOT
#define BUTTON_NEED_PULLUP

#define EXT_NOTIFY_OUT 8 // 10A relay

#define GPS_DEFAULT_NOT_PRESENT 1

// ST7796 LCD, 2.33" 480x222 - the same panel as the T-Lora Pager
#define TFT_CS 21
#define HAS_SPI_TFT 1
#define ST7796_CS TFT_CS
#define ST7796_RS 41  // DC
#define ST7796_SDA 11 // MOSI
#define ST7796_SCK 12
#define ST7796_MISO 13
#define ST7796_RESET -1
#define ST7796_BUSY -1
#define ST7796_BL 46
#define ST7796_SPI_HOST SPI2_HOST
#define TFT_BL 46
#define SPI_FREQUENCY 75000000
#define SPI_READ_FREQUENCY 16000000
#define TFT_WIDTH 222
#define TFT_HEIGHT 480
#define TFT_OFFSET_X 49
#define TFT_OFFSET_Y 0
// Landscape comes from TFTDisplay's default setRotation(3), which aligns the UI with the
// silkscreen - 180 degrees from LilyGo's test firmware. Deliberate, don't "fix" it.
#define TFT_OFFSET_ROTATION 0
#define SCREEN_ROTATE
#define SCREEN_TRANSITION_FRAMERATE 30
#define BRIGHTNESS_DEFAULT 130 // Medium Low Brightness
#define USE_TFTDISPLAY 1

// CST226SE touch - driver in src/platform/extra_variants/tbeam_displayshield/variant.cpp
#define HAS_CST226SE 1
#define HAS_TOUCHSCREEN 1
#define VARIANT_TOUCHSCREEN 1
#define USE_VIRTUAL_KEYBOARD 1
#define TOUCH_RST 47
#define SCREEN_TOUCH_INT 3
#define ENABLE_TOUCH_INT

// LoRa - HPD16A (SX1262)
#define USE_SX1262

#define LORA_SCK 12
#define LORA_MISO 13
#define LORA_MOSI 11
#define LORA_CS 14
#define LORA_RESET 42
#define LORA_DIO1 45

#define SX126X_CS LORA_CS
#define SX126X_DIO1 LORA_DIO1
#define SX126X_BUSY 38
#define SX126X_RESET LORA_RESET
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 1.8

// W5500 ethernet, sharing the LoRa SPI bus
#define HAS_ETHERNET 1
#define USE_WS5500 1
#define ETH_SHARED_SPI SPI

#define ETH_CS_PIN 10
#define ETH_INT_PIN 9
#define ETH_RST_PIN 48

// Isolated connectors, for the Serial module: RS232 TX 4 / RX 5, RS485 TX 17 / RX 18, CAN TX 6 / RX 7.
