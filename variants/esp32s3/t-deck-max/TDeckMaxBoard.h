#pragma once

#include <Arduino.h>
#include <stdint.h>

// Board identity
#define BOARD_NAME "T-Deck-MAX v0.3"
#define UI_T_DECK_PRO_VERSION "v3.1-260313"    // Software version
#define BOARD_T_DECK_PRO_VERSION "v3.3-250911" // Hardware version

// Common serial aliases used by multiple examples.
#define SerialMon Serial
#define SerialAT Serial1
#define SerialGPS Serial2

// I2C addresses
// Note: later T-Deck-MAX revisions use SY6970 instead of BQ25896.
#define BOARD_I2C_ADDR_ES8311 0x18     // ES8311   audio codec
#define BOARD_I2C_ADDR_TOUCH 0x1A      // CST328   touch controller
#define BOARD_I2C_ADDR_XL9555 0x20     // XL9555   IO expander
#define BOARD_I2C_ADDR_GYROSCOPDE 0x28 // BHI260AP gyroscope
#define BOARD_I2C_ADDR_KEYBOARD 0x34   // TCA8418  keyboard matrix controller
#define BOARD_I2C_ADDR_BQ27220 0x55    // BQ27220  fuel gauge
#define BOARD_I2C_ADDR_MOTOR 0x5A      // DRV2605  vibration motor driver
#define BOARD_I2C_ADDR_BQ25896 0x6B    // BQ25896  charger (deprecated)
#define BOARD_I2C_ADDR_SY6970 0x6A     // SY6970   charger

// Common I2C bus
#define BOARD_I2C_SDA 13
#define BOARD_I2C_SCL 14

// XL9555 IO expansion
#define BOARD_XL9555_INT (-1) // XL9555 INT is not connected to the ESP32-S3
#define BOARD_XL9555_SDA BOARD_I2C_SDA
#define BOARD_XL9555_SCL BOARD_I2C_SCL
#define BOARD_XL9555_00_6609_EN (0)    // HIGH: enable A7682E power
#define BOARD_XL9555_01_LORA_EN (1)    // HIGH: enable SX1262 power
#define BOARD_XL9555_02_GPS_EN (2)     // HIGH: enable GPS power
#define BOARD_XL9555_03_1V8_EN (3)     // HIGH: enable BHI260AP power
#define BOARD_XL9555_04_LORA_SEL (4)   // HIGH: internal antenna, LOW: external antenna
#define BOARD_XL9555_05_MOTOR_EN (5)   // HIGH: enable DRV2605 power
#define BOARD_XL9555_06_AMPLIFIER (6)  // HIGH: enable power amplifier
#define BOARD_XL9555_07_TOUCH_RST (7)  // LOW: reset touch
#define BOARD_XL9555_10_PWRKEY_EN (8)  // HIGH: enable A7682E POWERKEY
#define BOARD_XL9555_11_KEY_RST (9)    // LOW: reset keyboard
#define BOARD_XL9555_12_AUDIO_SEL (10) // HIGH: A7682E audio, LOW: ES8311 audio
#define BOARD_XL9555_13 (11)           // reserved
#define BOARD_XL9555_14 (12)           // reserved
#define BOARD_XL9555_15 (13)           // reserved
#define BOARD_XL9555_16 (14)           // reserved
#define BOARD_XL9555_17 (15)           // reserved

// XL9555 "virtual GPIO" encoding for drivers that expect a GPIO number.
#define XL9555_GPIO_BASE (0x100)
#define XL9555_GPIO(pin) (XL9555_GPIO_BASE + (pin))
#define XL9555_GPIO_IS(id) ((int)(id) >= XL9555_GPIO_BASE && (int)(id) < (XL9555_GPIO_BASE + 16))
#define XL9555_GPIO_TO_PIN(id) ((uint8_t)((id)-XL9555_GPIO_BASE))

// Keyboard
#define BOARD_KEYBOARD_SCL BOARD_I2C_SCL
#define BOARD_KEYBOARD_SDA BOARD_I2C_SDA
#define BOARD_KEYBOARD_INT 15
#define BOARD_KEYBOARD_LED 42
#define BOARD_KEYBOARD_RST BOARD_XL9555_11_KEY_RST

// Touch
#define BOARD_TOUCH_SCL BOARD_I2C_SCL
#define BOARD_TOUCH_SDA BOARD_I2C_SDA
#define BOARD_TOUCH_INT 12
#define BOARD_TOUCH_RST XL9555_GPIO(BOARD_XL9555_07_TOUCH_RST)

// Gyroscope
#define BOARD_GYROSCOPDE_SCL BOARD_I2C_SCL
#define BOARD_GYROSCOPDE_SDA BOARD_I2C_SDA
#define BOARD_GYROSCOPDE_INT 21
#define BOARD_GYROSCOPDE_RST -1

// Motor
#define BOARD_MOTOR_SCL BOARD_I2C_SCL
#define BOARD_MOTOR_SDA BOARD_I2C_SDA
#define BOARD_MOTOR_EN BOARD_XL9555_05_MOTOR_EN

// ES8311
#define BOARD_ES8311_SCL BOARD_I2C_SCL
#define BOARD_ES8311_SDA BOARD_I2C_SDA
#define BOARD_ES8311_MCLK 38
#define BOARD_ES8311_SCLK 39
#define BOARD_ES8311_ASDOUT 40
#define BOARD_ES8311_LRCK 18
#define BOARD_ES8311_DSDIN 17

// Shared SPI bus
#define BOARD_SPI_SCK 36
#define BOARD_SPI_MOSI 33
#define BOARD_SPI_MISO 47

// Display
#define LCD_HOR_SIZE 240
#define LCD_VER_SIZE 320
#define DISP_BUF_SIZE (LCD_HOR_SIZE * LCD_VER_SIZE)

#define BOARD_EPD_BL 41
#define BOARD_EPD_SCK BOARD_SPI_SCK
#define BOARD_EPD_MOSI BOARD_SPI_MOSI
#define BOARD_EPD_DC 35
#define BOARD_EPD_CS 34
#define BOARD_EPD_BUSY 37
#define BOARD_EPD_RST 9

// SD card
#define BOARD_SD_CS 48
#define BOARD_SD_SCK BOARD_SPI_SCK
#define BOARD_SD_MOSI BOARD_SPI_MOSI
#define BOARD_SD_MISO BOARD_SPI_MISO

// LoRa
#define BOARD_LORA_SCK BOARD_SPI_SCK
#define BOARD_LORA_MOSI BOARD_SPI_MOSI
#define BOARD_LORA_MISO BOARD_SPI_MISO
#define BOARD_LORA_CS 3
#define BOARD_LORA_BUSY 6
#define BOARD_LORA_RST 4
#define BOARD_LORA_INT 5

// GPS
#define BOARD_GPS_RXD 2
#define BOARD_GPS_TXD 16
#define BOARD_GPS_PPS 1

// A7682E modem
#define BOARD_A7682E_RI 7
#define BOARD_A7682E_ITR 8 // DTR
#define BOARD_A7682E_DTR BOARD_A7682E_ITR
#define BOARD_A7682E_RXD 10
#define BOARD_A7682E_TXD 11
#define BOARD_A7682E_PWRKEY BOARD_XL9555_10_PWRKEY_EN

// Boot pin
#define BOARD_BOOT_PIN 0
