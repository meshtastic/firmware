// ST7789 TFT LCD
#define ST7789_CS 12
#define ST7789_RS 11  // DC
#define ST7789_SDA 41 // MOSI
#define ST7789_SCK 40
#define ST7789_RESET -1
#define ST7789_MISO 38
#define ST7789_BUSY -1
#define ST7789_BL 42
#define ST7789_SPI_HOST SPI2_HOST
#define ST7789_BACKLIGHT_EN 42
#define SPI_FREQUENCY 40000000
#define SPI_READ_FREQUENCY 16000000
#define TFT_HEIGHT 320
#define TFT_WIDTH 240
#define TFT_OFFSET_X 0
#define TFT_OFFSET_Y 0
#define SCREEN_ROTATE
#define SCREEN_TRANSITION_FRAMERATE 1 // fps
#define SCREEN_TOUCH_INT 16
#define TOUCH_SLAVE_ADDRESS 0x5D // GT911

#define BUTTON_PIN 0
// #define BUTTON_NEED_PULLUP

#define HAS_GPS 0
#undef GPS_RX_PIN
#undef GPS_TX_PIN

// Have SPI interface SD card slot
#define HAS_SDCARD 1
#define SPI_MOSI (41)
#define SPI_SCK (40)
#define SPI_MISO (38)
#define SPI_CS (39)
#define SDCARD_CS SPI_CS

#define BATTERY_PIN 4 // A battery voltage measurement pin, voltage divider connected here to measure battery voltage
// ratio of voltage divider = 2.0 (RD2=100k, RD3=100k)
#define ADC_MULTIPLIER 2.11 // 2.0 + 10% for correction of display undervoltage.
#define ADC_CHANNEL ADC1_GPIO1_CHANNEL

// keyboard
#define I2C_SDA 18 // I2C pins for this board
#define I2C_SCL 8
#define BOARD_POWERON 10 // must be set to HIGH
#define KB_SLAVE_ADDRESS 0x55
#define KB_BL_PIN 46 // INT, set to INPUT
#define KB_UP 2
#define KB_DOWN 3
#define KB_LEFT 1
#define KB_RIGHT 15

#define USE_SX1262
#define USE_SX1268

#define RF95_SCK 40
#define RF95_MISO 38
#define RF95_MOSI 41
#define RF95_NSS 9

#define LORA_DIO0 -1 // a No connect on the SX1262 module
#define LORA_RESET 17
#define LORA_DIO1 45 // SX1262 IRQ
#define LORA_DIO2 13 // SX1262 BUSY
#define LORA_DIO3    // Not connected on PCB, but internally on the TTGO SX1262, if DIO3 is high the TXCO is enabled

#define SX126X_CS RF95_NSS // FIXME - we really should define LORA_CS instead
#define SX126X_DIO1 LORA_DIO1
#define SX126X_BUSY LORA_DIO2
#define SX126X_RESET LORA_RESET
#define SX126X_E22 // Not really an E22 but TTGO seems to be trying to clone that
// Internally the TTGO module hooks the SX1262-DIO2 in to control the TX/RX switch (which is the default for the sx1262interface
// code)
