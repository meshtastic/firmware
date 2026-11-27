#pragma once

#include "../seeed_xiao_s3/variant.h"

#undef GPS_L76K
#undef PIN_GPS_STANDBY
#undef PIN_SERIAL1_RX
#undef PIN_SERIAL1_TX
#undef USCREEN_SSD1306

#define PIN_SERIAL1_RX GPS_RX_PIN
#define PIN_SERIAL1_TX GPS_TX_PIN
#define HAS_SCREEN 1
#define HAS_SPI_TFT 1
#define USE_ST7789
#define ST7789_SHARED_SPI
#define ST7789_NSS 1
#define ST7789_RS 3
#define ST7789_RESET 4
#define ST7789_SDA LORA_MOSI
#define ST7789_MISO LORA_MISO
#define ST7789_SCK LORA_SCK
#define TFT_WIDTH 320
#define TFT_HEIGHT 170
