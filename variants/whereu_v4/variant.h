#define HAS_GPS 1
#undef GPS_RX_PIN
#undef GPS_TX_PIN
#define GPS_RX_PIN 20
#define GPS_TX_PIN 4
#define GPS_FORCEON 13
// #define PIN_GPS_EN GPS_FORCEON // GPS power enable pin?
#define PIN_GPS_STANDBY GPS_FORCEON // Alternative check logic (note we need to send sleep to gps aswell as set this low)
#define HAS_SCREEN 1

#define I2C_SDA 25
#define I2C_SCL 26

// #define HAS_SDCARD
// #define SDCARD_USE_SPI1

// #define USE_SSD1306
#define ACC_INTERUPT 39
#define POWER_INTERUPT 34
#define TOUCH_INTERUPT 35

// #define LED_PIN 46
// #define LED_STATE_ON 0 // State when LED is litted

#define BUTTON_PIN 36
#define BUTTON_PIN_ALT 38
// #define USE_RF95   // RFM95/SX127x

#undef RF95_SCK
#undef RF95_MISO
#undef RF95_MOSI
#undef RF95_NSS
// WaveShare Core1262-868M OK
// https://www.waveshare.com/wiki/Core1262-868M
#define USE_SX1262

#ifdef USE_SX1262
#define LORA_DIO1 22
#define LORA_BUSY 47
#define RF95_MISO 19
#define RF95_SCK 7
#define RF95_MOSI 8
#define RF95_NSS 5
#define LORA_RESET 21
#define SX126X_CS RF95_NSS
#define SX126X_DIO1 LORA_DIO1
#define SX126X_BUSY LORA_BUSY
#define SX126X_RESET LORA_RESET
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 1.8
// #define SX126X_DIO3_TCXO_VOLTAGE 1.8
#endif
#define LCD_MISO RF95_MISO
#define LCD_SCK RF95_SCK
#define LCD_MOSI RF95_MOSI
#define LCD_CS 15
#define LCD_RST 2
#define LCD_DC 12
#define LCD_PSU 14
#define LCD_BL 27

// HAS_PMU (convert to use INA)
// ST7789 TFT LCD

#define ST7789_CS LCD_CS
#define ST7789_RS LCD_DC     // DC
#define ST7789_SDA RF95_MOSI // MOSI
#define ST7789_SCK RF95_SCK
#define ST7789_RESET LCD_RST
#define ST7789_MISO LCD_MISO
#define ST7789_BUSY -1
#define ST7789_BL LCD_BL
#define ST7789_SPI_HOST SPI3_HOST
#define ST7789_BACKLIGHT_EN LCD_PSU // This turns on
#define SPI_FREQUENCY 40000000
#define SPI_READ_FREQUENCY 16000000
#define TFT_HEIGHT 240
#define TFT_WIDTH 240
#define TFT_OFFSET_X 0
#define TFT_OFFSET_Y 0
#define TFT_OFFSET_ROTATION 2
#define SCREEN_ROTATE
#define SCREEN_TRANSITION_FRAMERATE 15 // fps

// #define USE_EINK
/*
 * eink display pins
 */
// #define PIN_EINK_CS
// #define PIN_EINK_BUSY
// #define PIN_EINK_DC
// #define PIN_EINK_RES    (-1)
// #define PIN_EINK_SCLK   3
// #define PIN_EINK_MOSI   4
#define HAS_TOUCHSCREEN 0
#define SCREEN_TOUCH_INT 16
// #define SCREEN_TOUCH_USE_I2C1
#define TOUCH_I2C_PORT 1
#define TOUCH_SLAVE_ADDRESS 0x40