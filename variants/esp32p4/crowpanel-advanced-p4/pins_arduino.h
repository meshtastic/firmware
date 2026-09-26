#ifndef Pins_Arduino_h
#define Pins_Arduino_h

#include "soc/soc_caps.h"
#include <stdint.h>

// BOOT_MODE 35
// BOOT_MODE2 36 pullup

static const uint8_t TX = 37;
static const uint8_t RX = 38;

#if defined(CROWPANEL_ADV_P4_50)
static const uint8_t SDA = 45;
static const uint8_t SCL = 46;
#else
static const uint8_t SDA = 45;
static const uint8_t SCL = 46;
#endif

// Use GPIOs 36 or lower on the P4 DevKit to avoid LDO power issues with high numbered GPIOs.
static const uint8_t SS = 30;
static const uint8_t MOSI = 48;
static const uint8_t MISO = 47;
static const uint8_t SCK = 26;

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

/* ESP32-P4 EV Function board specific definitions */
// ETH
// #define ETH_PHY_TYPE    ETH_PHY_TLK110
// #define ETH_PHY_ADDR    1
// #define ETH_PHY_MDC     31
// #define ETH_PHY_MDIO    52
// #define ETH_PHY_POWER   51
// #define ETH_RMII_TX_EN  49
// #define ETH_RMII_TX0    34
// #define ETH_RMII_TX1    35
// #define ETH_RMII_RX0    29
// #define ETH_RMII_RX1_EN 30
// #define ETH_RMII_CRS_DV 28
// #define ETH_RMII_CLK    50
// #define ETH_CLK_MODE    EMAC_CLK_EXT_IN

// SDMMC
#define BOARD_HAS_SDMMC
#define BOARD_HAS_SD_SDMMC
#define SDMMC_CMD 44 // SD_CMD/MOSI
#define SDMMC_CLK 43 // SD_CLK
#define SDMMC_D0 39  // SD_D0/MISO
#define SD_CS -1     // No CS pin in SDMMC mode

#if defined(CROWPANEL_ADV_P4_50)
#define BOARD_SDMMC_SLOT 1
// Workaround for Arduino-ESP32 P4 SPI LDO auto-config on SDMMC slot0 pins (47/48).
// Use a valid GPIO (aligned to variant LoRa CS) and pre-tag it in initVariant()
// so setLDOPower() short-circuits.
#define BOARD_SDMMC_POWER_PIN 30
#else
#define BOARD_SDMMC_POWER_PIN 10
#define BOARD_SDMMC_SLOT 0
#endif
#define BOARD_SDMMC_POWER_CHANNEL 4
#define BOARD_SDMMC_POWER_ON_LEVEL HIGH

// BT/WIFI - ESP32C6
#define BOARD_HAS_SDIO_ESP_HOSTED
#ifdef CROWPANEL_ADV_P4_50
// CrowPanel Advanced P4 50": 4-bit SDIO on Slot 1 with GPIO 53/54/52/51/50/49
#define BOARD_SDIO_ESP_HOSTED_CLK 53
#define BOARD_SDIO_ESP_HOSTED_CMD 54
#define BOARD_SDIO_ESP_HOSTED_D0 52
#define BOARD_SDIO_ESP_HOSTED_D1 51
#define BOARD_SDIO_ESP_HOSTED_D2 50
#define BOARD_SDIO_ESP_HOSTED_D3 49
#define BOARD_SDIO_ESP_HOSTED_RESET 20
#else
// CrowPanel Advanced P4 70/90/101": 1-bit SDIO on Slot 1 with GPIO 18/19/14/15
#define BOARD_SDIO_ESP_HOSTED_CLK 18
#define BOARD_SDIO_ESP_HOSTED_CMD 19
#define BOARD_SDIO_ESP_HOSTED_D0 14
#define BOARD_SDIO_ESP_HOSTED_D1 15
#define BOARD_SDIO_ESP_HOSTED_D2 16
#define BOARD_SDIO_ESP_HOSTED_D3 17
#define BOARD_SDIO_ESP_HOSTED_RESET 32
#endif

#endif /* Pins_Arduino_h */
