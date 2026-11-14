#ifndef Pins_Arduino_h
#define Pins_Arduino_h

#include "soc/soc_caps.h"
#include <stdint.h>

static const uint8_t TX = 37;
static const uint8_t RX = 38;

static const uint8_t SDA = 46;
static const uint8_t SCL = 45;

// Use GPIOs 36 or lower on the P4 DevKit to avoid LDO power issues with high numbered GPIOs.
static const uint8_t SS = 30;
static const uint8_t MOSI = 48;
static const uint8_t MISO = 47;
static const uint8_t SCK = 33;

static const uint8_t A0 = 16;
static const uint8_t A1 = 17;
static const uint8_t A2 = 18;
static const uint8_t A3 = 19;
static const uint8_t A4 = 20;
static const uint8_t A5 = 21;
static const uint8_t A6 = 22;
static const uint8_t A7 = 23;
static const uint8_t A8 = 49;
static const uint8_t A9 = 50;
static const uint8_t A10 = 51;
static const uint8_t A11 = 52;
static const uint8_t A12 = 53;
static const uint8_t A13 = 54;

static const uint8_t T0 = 2;
static const uint8_t T1 = 3;
static const uint8_t T2 = 4;
static const uint8_t T3 = 5;
static const uint8_t T4 = 6;
static const uint8_t T5 = 7;
static const uint8_t T6 = 8;
static const uint8_t T7 = 9;
static const uint8_t T8 = 10;
static const uint8_t T9 = 11;
static const uint8_t T10 = 12;
static const uint8_t T11 = 13;
static const uint8_t T12 = 14;
static const uint8_t T13 = 15;

#define BOARD_SDMMC_SLOT 0
// Workaround for Arduino-ESP32 P4 SPI LDO auto-config on SDMMC slot0 pins (47/48).
// Use a valid GPIO (aligned to variant LoRa CS) and pre-tag it in initVariant()
// so setLDOPower() short-circuits.
#define BOARD_SDMMC_POWER_PIN -1

#define BOARD_SDMMC_POWER_CHANNEL 4
#define BOARD_SDMMC_POWER_ON_LEVEL HIGH

// BT/WIFI - ESP32C6
#define BOARD_HAS_SDIO_ESP_HOSTED
// CrowPanel Advanced P4 50": 4-bit SDIO on Slot 1 with GPIO 53/54/52/51/50/49
#define BOARD_SDIO_ESP_HOSTED_CLK (18)
#define BOARD_SDIO_ESP_HOSTED_CMD (19)
#define BOARD_SDIO_ESP_HOSTED_D0 (17)
#define BOARD_SDIO_ESP_HOSTED_D1 (16)
#define BOARD_SDIO_ESP_HOSTED_D2 (15)
#define BOARD_SDIO_ESP_HOSTED_D3 (14)
#define BOARD_SDIO_ESP_HOSTED_RESET (7)

#endif /* Pins_Arduino_h */
