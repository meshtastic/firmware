// Makerfabs Nomad Terminal (ESP32-S3-WROOM-1-N16R8 + LR1121, 3.5" ILI9488 / FT6236)

// Soft power latch: KEY powers the board up, POWER_ON_OFF high keeps it on, low cuts battery power
#define PIN_POWER_EN 2
// Switched 3V3 rail for LCD and GPS
#define VEXT_ENABLE 18
#define VEXT_ON_VALUE HIGH

#define BUTTON_PIN 21

#define PIN_BUZZER 17

#define EXT_PWR_DETECT 15

#define HAS_CW2015 1

#define I2C_SDA 39
#define I2C_SCL 38

#define PCF8563_RTC 0x51

#define HAS_GPS 1
#define GPS_RX_PIN 47
#define GPS_TX_PIN 48
#define PIN_GPS_EN 40
#define GPS_EN_ACTIVE HIGH

// LR1121 and SD card share this bus, the LCD has its own
#define SPI_SCK 6
#define SPI_MISO 20
#define SPI_MOSI 19

#define LORA_SCK SPI_SCK
#define LORA_MISO SPI_MISO
#define LORA_MOSI SPI_MOSI
#define LORA_CS 11
#define LORA_RESET 14
#define LORA_DIO1 13

#define USE_LR1121
#define LR1121_IRQ_PIN LORA_DIO1
#define LR1121_NRESET_PIN LORA_RESET
#define LR1121_BUSY_PIN 8
#define LR1121_SPI_NSS_PIN LORA_CS
#define LR1121_SPI_SCK_PIN LORA_SCK
#define LR1121_SPI_MOSI_PIN LORA_MOSI
#define LR1121_SPI_MISO_PIN LORA_MISO
#define LR11X0_DIO3_TCXO_VOLTAGE 1.8
#define LR11X0_DIO_AS_RF_SWITCH

// BaseUI panel; MUI takes it from the LGFX_* build flags
#define USE_TFTDISPLAY 1
#define HAS_SPI_TFT 1
#define ILI9488_SPI_HOST SPI3_HOST
#define ILI9488_CS 4
#define ILI9488_RS 5
#define ILI9488_SDA 10
#define ILI9488_SCK 16
#define ILI9488_MISO 12
#define ILI9488_RESET 7
#define ILI9488_BL 9
#define SPI_FREQUENCY 40000000
#define SPI_READ_FREQUENCY 16000000
#define TFT_WIDTH 320
#define TFT_HEIGHT 480
#define TFT_OFFSET_X 0
#define TFT_OFFSET_Y 0
#define TFT_OFFSET_ROTATION 3 // portrait with connect()'s setRotation(3); 0 = landscape
#define TFT_INVERT false
#define SCREEN_TRANSITION_FRAMERATE 10
// FT6236 touch on the sensor I2C bus
#define HAS_TOUCHSCREEN 1
#define SCREEN_TOUCH_INT 41
#define TOUCH_I2C_PORT 0
#define TOUCH_SLAVE_ADDRESS 0x38
#define USE_VIRTUAL_KEYBOARD 1
