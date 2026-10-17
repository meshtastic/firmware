#ifndef OLEDDISPLAYFONTSGR_h
#define OLEDDISPLAYFONTSGR_h

#ifdef ARDUINO
#include <Arduino.h>
#elif __MBED__
#define PROGMEM
#endif

// Greek (CP-1253 slots, Ώ at 0xAA); replaces Latin-1 letters 0xC0-0xFF, so é/ü etc. are unavailable with OLED_GR.
extern const uint8_t ArialMT_Plain_10_GR[] PROGMEM;
extern const uint8_t ArialMT_Plain_16_GR[] PROGMEM;
extern const uint8_t ArialMT_Plain_24_GR[] PROGMEM;
#endif
