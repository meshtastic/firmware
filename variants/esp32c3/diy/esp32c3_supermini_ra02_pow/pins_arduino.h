#pragma once

#include <stdint.h>

// Arduino core defaults for the specific wired ESP32-C3 Super Mini board.
// The module's LoRa pins are also explicitly set in variant.h.
static const uint8_t TX = 21;
static const uint8_t RX = 20;
static const uint8_t SDA = 1;
static const uint8_t SCL = 2;
static const uint8_t SS = 10;
static const uint8_t MOSI = 7;
static const uint8_t MISO = 5;
static const uint8_t SCK = 6;
static const uint8_t A0 = 0;
static const uint8_t A1 = 1;
static const uint8_t A2 = 2;
static const uint8_t A3 = 3;
static const uint8_t A4 = 4;
static const uint8_t A5 = 5;
