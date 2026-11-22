#ifndef Pins_Arduino_h
#define Pins_Arduino_h

#include "soc/soc_caps.h"
#include <stdint.h>

// LilyGO T-Display-P4 V1.0. This file is also included by Arduino C core files: keep it C-compatible.
// GPIO35 = BOOT strap + USB-UART DTR + RMII_TXD1, GPIO36 = strap with pull-up: never configure them.

static const uint8_t TX = 37; // UART0 -> USB-UART bridge
static const uint8_t RX = 38;

static const uint8_t SDA = 7; // I2C bus A: XL9535, PCF8563, BQ27220, GT9895
static const uint8_t SCL = 8;

static const uint8_t SS = 24; // LoRa CS
static const uint8_t MOSI = 3;
static const uint8_t MISO = 4;
static const uint8_t SCK = 2;

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

// No ETH_PHY_* / ETH_RMII_* block: the IP101 PHY is held in reset (XL9535 IO5) and Ethernet is unused.

// SD card: SDMMC slot 0 (CLK 43, CMD 44, D0 39), mounted by device-ui.
// No BOARD_SDMMC_POWER_* and no BOARD_PERIMAN_IO_LDO_*: SD VDD is XL9535 IO15 and LDO_VO4 is
// acquired in earlyInitVariant().
#define BOARD_HAS_SDMMC
#define BOARD_SDMMC_SLOT 0

// ESP32-C6 co-processor: esp_hosted over SDIO, SDMMC slot 1
#define BOARD_HAS_SDIO_ESP_HOSTED
#define BOARD_SDIO_ESP_HOSTED_CLK 18
#define BOARD_SDIO_ESP_HOSTED_CMD 19
#define BOARD_SDIO_ESP_HOSTED_D0 14
#define BOARD_SDIO_ESP_HOSTED_D1 15
#define BOARD_SDIO_ESP_HOSTED_D2 16
#define BOARD_SDIO_ESP_HOSTED_D3 17
// DUMMY reset pin: GPIO31 = RMII_MDC, an input of the PHY that is held in reset. The real C6 EN line
// is XL9535 IO14, driven by variantHostedSlaveReset() in variant.cpp.
// TODO(hw-verify): esp_hosted drives this pin; confirm GPIO31 is harmless at either level.
#define BOARD_SDIO_ESP_HOSTED_RESET 31

#endif /* Pins_Arduino_h */
