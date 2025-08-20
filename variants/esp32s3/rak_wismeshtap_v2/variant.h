#ifndef _VARIANT_RAK_WISMESHTAP_V2_H
#define _VARIANT_RAK_WISMESHTAP_V2_H

#define I2C_SDA 9
#define I2C_SCL 40

#define USE_SX1262

#define LORA_SCK 5
#define LORA_MISO 3
#define LORA_MOSI 6
#define LORA_CS 7
#define LORA_RESET 8

#ifdef USE_SX1262
#define SX126X_CS LORA_CS
#define SX126X_DIO1 47
#define SX126X_BUSY 48
#define SX126X_RESET LORA_RESET
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 1.8
#endif

#define SX126X_POWER_EN (4)

#define PIN_POWER_EN PIN_3V3_EN
#define PIN_3V3_EN (14)

#define LED_GREEN 46
#define LED_BLUE 45

#define PIN_LED1 LED_GREEN
#define PIN_LED2 LED_BLUE

#define LED_CONN LED_BLUE
#define LED_PIN LED_GREEN
#define ledOff(pin) pinMode(pin, INPUT)

#define LED_STATE_ON 1 // State when LED is litted

#define HAS_GPS 1
#define GPS_TX_PIN 43
#define GPS_RX_PIN 44

// #define BATTERY_PIN 10
// #define ADC_CHANNEL ADC1_GPIO10_CHANNEL
// #define ADC_MULTIPLIER   1.667

#define SPI_MOSI (11)
#define SPI_SCK (13)
#define SPI_MISO (10)
#define SPI_CS (12)

// LORA SPI2

#define ST7789_CS SPI_CS
#define ST7789_BL 41
#define SPI_FREQUENCY 40000000
#define SPI_READ_FREQUENCY 40000000
#define ST7789_SCK SPI_SCK   // Set SPI SCLK pin number
#define ST7789_SDA SPI_MOSI  // Set SPI MOSI pin number
#define ST7789_MISO SPI_MISO // Set SPI MISO pin number (-1 = disable)
#define ST7789_RS 42
#define ST7789_CS SPI_CS
#define TFT_WIDTH 240
#define TFT_HEIGHT 320
#define TFT_OFFSET_X 0
#define TFT_OFFSET_Y 0
#define TFT_OFFSET_ROTATION 2
#define ST7789_SPI_HOST SPI3_HOST

#define SCREEN_ROTATE

#define HAS_TOUCHSCREEN 1
#define SCREEN_TOUCH_INT 39
#define TOUCH_SLAVE_ADDRESS 0x38 // RAK14014_FT6336U
#define TOUCH_I2C_PORT 0

#define HAS_BUTTON 1
#define BUTTON_PIN 0

#define CANNED_MESSAGE_MODULE_ENABLE 1
#define USE_VIRTUAL_KEYBOARD 1

#define BATTERY_PIN 1
#define ADC_CHANNEL ADC1_GPIO1_CHANNEL
#define ADC_MULTIPLIER 1.667

#define PIN_BUZZER 38

#define SDCARD_USE_SPI1 1
// #define HAS_SDCARD 1
#define SDCARD_CS  2

#define SD_SPI_FREQUENCY 16000000

#endif