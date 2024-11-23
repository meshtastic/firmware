#define I2C_SDA 39
#define I2C_SCL 40

#define BUTTON_PIN 38
// #define BUTTON_NEED_PULLUP

// #define BATTERY_PIN 27 // A battery voltage measurement pin, voltage divider connected here to measure battery voltage
// #define ADC_CHANNEL ADC1_GPIO27_CHANNEL
// #define ADC_MULTIPLIER 2

// ST7701 TFT LCD
#define ST7701_CS (4 | IO_EXPANDER)
#define ST7701_RS -1  // DC
#define ST7701_SDA 48 // MOSI
#define ST7701_SCK 41
#define ST7701_RESET (5 | IO_EXPANDER)
#define ST7701_MISO 47
#define ST7701_BUSY -1
#define ST7701_BL 45
#define ST7701_SPI_HOST SPI2_HOST
#define ST7701_BACKLIGHT_EN 45
#define SPI_FREQUENCY 20000000
#define SPI_READ_FREQUENCY 16000000
#define TFT_HEIGHT 480
#define TFT_WIDTH 480
#define TFT_OFFSET_X 0
#define TFT_OFFSET_Y 0
#define TFT_OFFSET_ROTATION 0
#define TFT_BL 45
#define SCREEN_ROTATE
#define SCREEN_TRANSITION_FRAMERATE 5 // fps

#define HAS_TOUCHSCREEN 1
#define SCREEN_TOUCH_INT (6 | IO_EXPANDER)
#define SCREEN_TOUCH_RST (7 | IO_EXPANDER)
#define TOUCH_I2C_PORT 0
#define TOUCH_SLAVE_ADDRESS 0x48

// in future, we may want to add a buzzer and add all sensors to the indicator via a data protocol for now only GPS is supported
// // Buzzer
// #define PIN_BUZZER 19

#define GPS_RX_PIN 20
#define GPS_TX_PIN 19
#define HAS_GPS 1

#define USE_SX1262
#define USE_SX1268

#define LORA_SCK 41
#define LORA_MISO 47
#define LORA_MOSI 48
#define LORA_CS (0 | IO_EXPANDER)

#define LORA_DIO0 -1 // a no connect on the SX1262 module
#define LORA_RESET (1 | IO_EXPANDER)
#define LORA_DIO1 (3 | IO_EXPANDER) // SX1262 IRQ
#define LORA_DIO2 (2 | IO_EXPANDER) // SX1262 BUSY
#define LORA_DIO3

#define SX126X_CS LORA_CS
#define SX126X_DIO1 LORA_DIO1
#define SX126X_BUSY LORA_DIO2
#define SX126X_RESET LORA_RESET
#define SX126X_DIO2_AS_RF_SWITCH

#define USE_VIRTUAL_KEYBOARD 1
#define DISPLAY_CLOCK_FRAME 1
