#define I2C_SDA 39
#define I2C_SCL 40

#define BUTTON_PIN 38
// #define BUTTON_NEED_PULLUP

// #define BATTERY_PIN 27 // A battery voltage measurement pin, voltage divider connected here to measure battery voltage
// #define ADC_CHANNEL ADC1_GPIO27_CHANNEL
// #define ADC_MULTIPLIER 2

// ST7789 TFT LCD
#define ST7789_CS -1  // IO04
#define ST7789_RS -1  // DC
#define ST7789_SDA 48 // MOSI
#define ST7789_SCK 41
#define ST7789_RESET -1 // IO05
#define ST7789_MISO 47
#define ST7789_BUSY -1
#define ST7789_BL 45
#define ST7789_SPI_HOST SPI2_HOST
#define ST7789_BACKLIGHT_EN 45
#define SPI_FREQUENCY 20000000
#define SPI_READ_FREQUENCY 16000000
#define TFT_HEIGHT 480
#define TFT_WIDTH 480
#define TFT_OFFSET_X 0
#define TFT_OFFSET_Y 0
#define TFT_OFFSET_ROTATION 2
#define TFT_BL ST7789_BACKLIGHT_EN
#define SCREEN_ROTATE
#define SCREEN_TRANSITION_FRAMERATE 5 // fps

#define HAS_TOUCHSCREEN 1
#define SCREEN_TOUCH_INT -1 // IO06
#define TOUCH_I2C_PORT 0x55
#define TOUCH_SLAVE_ADDRESS -1

// Buzzer
#define PIN_BUZZER 19

#define HAS_GPS 0
#undef GPS_RX_PIN
#undef GPS_TX_PIN

#define USE_SX1262
#define USE_SX1268

#define LORA_SCK 41
#define LORA_MISO 47
#define LORA_MOSI 48
#define LORA_CS -1 // IO00

#define LORA_DIO0 -1  // a no connect on the SX1262 module
#define LORA_RESET -1 // IO01
#define LORA_DIO1 -1  // IO03 SX1262 IRQ
#define LORA_DIO2 -1  // IO02 SX1262 BUSY
#define LORA_DIO3

#define SX126X_CS LORA_CS
#define SX126X_DIO1 LORA_DIO1
#define SX126X_BUSY LORA_DIO2
#define SX126X_RESET LORA_RESET
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 1.8
