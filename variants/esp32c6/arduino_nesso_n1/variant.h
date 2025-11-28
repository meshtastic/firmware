void c6l_init();
void gpio_ext_set(uint8_t address, uint8_t pin, bool value);
uint8_t gpio_ext_get(uint8_t address, uint8_t pin);

#define HAS_GPS 0
#define GPS_RX_PIN -1
#define GPS_TX_PIN -1

#define I2C_SDA 10
#define I2C_SCL 8

#define LCD_CS 17
#define LCD_RS 16
#define SYS_IRQ 3

#define MOSI 21
#define MISO 22
#define SCK 20

#define PIN_BUZZER 11

#define IO_EXPANDER 0x40
#define LCD_BACKLIGHT 0x106

// #define BUTTON_PIN 9
#define BUTTON_EXTENDER

#undef LORA_SCK
#undef LORA_MISO
#undef LORA_MOSI
#undef LORA_CS

// battery charger BQ25896
//#define HAS_PPM 1
//#define XPOWERS_CHIP_BQ25896

// battery quality management BQ27220
#define HAS_BQ27220 1
#define BQ27220_I2C_SDA I2C_SDA
#define BQ27220_I2C_SCL I2C_SCL
#define BQ27220_DESIGN_CAPACITY 250

// WaveShare Core1262-868M OK
// https://www.waveshare.com/wiki/Core1262-868M
#define USE_SX1262

#define LORA_MISO 22
#define LORA_SCK 20
#define LORA_MOSI 21
#define LORA_CS 23
#define LORA_RESET RADIOLIB_NC
#define LORA_DIO1 15
#define LORA_BUSY 19
#define SX126X_CS LORA_CS
#define SX126X_DIO1 LORA_DIO1
#define SX126X_BUSY LORA_BUSY
#define SX126X_RESET LORA_RESET
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 3.0

#define ST7789_DRIVER
#define ST7789_CS 17
#define ST7789_RS 16
#define ST7789_SDA 21
#define ST7789_SCK 20
#define ST7789_RESET -1
#define ST7789_MISO 22
#define ST7789_BUSY -1
#define ST7789_SPI_HOST SPI2_HOST
#define SPI_FREQUENCY 40000000
#define SPI_READ_FREQUENCY 16000000
#define TFT_HEIGHT 240
#define TFT_WIDTH 135
#define TFT_OFFSET_X 52
#define TFT_OFFSET_Y 40
#define TFT_OFFSET_ROTATION 1
#define SCREEN_TRANSITION_FRAMERATE 10
#define BRIGHTNESS_DEFAULT 130
#define HAS_TOUCHSCREEN 1
#define TOUCH_I2C_PORT 0
#define TOUCH_SLAVE_ADDRESS 0x38
#define SCREEN_TOUCH_INT 3
#define TFT_BL_EXT (LCD_BACKLIGHT | IO_EXPANDER)
