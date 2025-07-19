// meshtastic/firmware/variants/unphone/variant.h

#pragma once

#define SPI_SCK 39
#define SPI_MOSI 40
#define SPI_MISO 41

// We use the RFM95W LoRa module
#define USE_RF95
#define LORA_SCK SPI_SCK
#define LORA_MOSI SPI_MOSI
#define LORA_MISO SPI_MISO
#define LORA_CS 44
#define LORA_DIO0 10 // AKA LORA_IRQ
#define LORA_RESET 42
#define LORA_DIO1 11
#define LORA_DIO2 RADIOLIB_NC // Not really used

// HX8357 TFT LCD
#define HX8357_CS 48
#define HX8357_RS 47 // AKA DC
#define HX8357_RESET 46
#define HX8357_SCK SPI_SCK
#define HX8357_MOSI SPI_MOSI
#define HX8357_MISO SPI_MISO
#define HX8357_BUSY -1
#define HX8357_SPI_HOST SPI2_HOST
#define SPI_FREQUENCY 40000000
#define SPI_READ_FREQUENCY 16000000
#define TFT_HEIGHT 480
#define TFT_WIDTH 320
#define TFT_OFFSET_X 0
#define TFT_OFFSET_Y 0
#define TFT_OFFSET_ROTATION 6 // unPhone's screen wired unusually, 0 typical
#define TFT_INVERT false
#define SCREEN_ROTATE true
#define SCREEN_TRANSITION_FRAMERATE 5

#define HAS_TOUCHSCREEN 1
#define USE_XPT2046 1
#define TOUCH_CS 38

#define USE_POWERSAVE
#define SLEEP_TIME 180

#define HAS_GPS                                                                                                                  \
    0 // the unphone doesn't have a gps module by default (though
      // GPS featherwing -- https://www.adafruit.com/product/3133
      // -- can be added)
#undef GPS_RX_PIN
#undef GPS_TX_PIN

#define SD_SPI_FREQUENCY 25000000
#define SDCARD_CS 43

#define LED_PIN 13     // the red part of the RGB LED
#define LED_STATE_ON 0 // State when LED is lit

#define ALT_BUTTON_PIN 21    // Button 3 - square - top button in landscape mode
#define BUTTON_PIN 0         // Circle button
#define BUTTON_NEED_PULLUP   // we do need a helping hand up
#define CANCEL_BUTTON_PIN 45 // Button 1 - triangle - bottom button in landscape mode
#define CANCEL_BUTTON_ACTIVE_LOW true
#define CANCEL_BUTTON_ACTIVE_PULLUP true

#define I2C_SDA 3 // I2C pins for this board
#define I2C_SCL 4

#define LSM6DS3_WAKE_THRESH 5 // higher values reduce the sensitivity of the wake threshold

// ratio of voltage divider = 3.20 (R1=100k, R2=220k)
// #define ADC_MULTIPLIER 3.2

// #define BATTERY_PIN 13 // battery V measurement pin; vbat divider is here
// #define ADC_CHANNEL ADC2_GPIO13_CHANNEL
// #define BAT_MEASURE_ADC_UNIT 2