#define SATELLITE_IMAGE_WIDTH 16
#define SATELLITE_IMAGE_HEIGHT 15
const uint8_t SATELLITE_IMAGE[] PROGMEM = {
    0x00, 0x08, 0x00, 0x1C, 0x00, 0x0E, 0x20, 0x07, 0x70, 0x02, 0xF8, 0x00,
    0xF0, 0x01, 0xE0, 0x03, 0xC8, 0x01, 0x9C, 0x54, 0x0E, 0x52, 0x07, 0x48,
    0x02, 0x26, 0x00, 0x10, 0x00, 0x0E
};


#include "icon.xbm"

#if 0
const uint8_t activeSymbol[] PROGMEM = {
    B00000000,
    B00000000,
    B00011000,
    B00100100,
    B01000010,
    B01000010,
    B00100100,
    B00011000
};

const uint8_t inactiveSymbol[] PROGMEM = {
    B00000000,
    B00000000,
    B00000000,
    B00000000,
    B00011000,
    B00011000,
    B00000000,
    B00000000
};
#endif