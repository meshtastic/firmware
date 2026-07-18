#pragma once

#ifdef ARDUINO
#include <Arduino.h>
#endif
#include <stdint.h>

// FontUTF8 structure definition
// This structure is used by all UTF-8 font files
typedef struct {
    const uint16_t* map;      // Code point mapping table (sorted in ascending order)
    const uint8_t* data;      // Glyph pixel data
    uint16_t count;           // Number of characters
    uint8_t w;                // Character width (pixels)
    uint8_t h;                // Character height (pixels)
} FontUTF8;
