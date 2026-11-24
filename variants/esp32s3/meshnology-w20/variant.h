#pragma once

#define I2C_SDA 8
#define I2C_SCL 7

#define HAS_CW2217 1
#define HAS_IP5356 1

#define BUTTON_PIN 0
#define ALT_BUTTON_PIN 48
#define LED_POWER 3
#define LED_NOTIFICATION 21
#define LED_STATE_ON HIGH

#define HAS_TOUCHSCREEN 1
#define SCREEN_TOUCH_INT 18
#define ENABLE_TOUCH_INT
#define PIN_TOUCH_RESET 17

#define PIN_EINK_CS 45
#define PIN_EINK_BUSY 40
#define PIN_EINK_DC 42
#define PIN_EINK_RES 41
#define PIN_EINK_SCLK 46
#define PIN_EINK_MOSI 47

// GPIO1 is shared with LoRa chip select, so the L76K reset pad is unused.
#define GPS_RX_PIN 44
#define GPS_TX_PIN 43
#define GPS_BAUDRATE 9600
#define PIN_GPS_EN 6
#define PIN_GPS_STANDBY 2

#define USE_LR2021
#define LORA_CS 1
#define LORA_SCK 12
#define LORA_MOSI 13
#define LORA_MISO 11
#define LORA_DIO1 5

#define LR2021_SPI_NSS_PIN LORA_CS
#define LR2021_IRQ_PIN LORA_DIO1
#define LR2021_NRESET_PIN 14
#define LR2021_BUSY_PIN 4
#define LR2021_POWER_EN 39
#define LR2021_IRQ_DIO_NUM 9
#define LR2021_DIO3_TCXO_VOLTAGE 3.0f
#define LR2021_DIO_AS_RF_SWITCH
#define LR2021_MAX_POWER 22
#define LR2021_MAX_POWER_HF 0
