#ifndef OLEDDISPLAYFONTSCS_h
#define OLEDDISPLAYFONTSCS_h

#ifdef ARDUINO
#include <Arduino.h>
#elif __MBED__
#define PROGMEM
#endif

/**
 * Localization for Czech and Slovak language containing glyphs with diacritic.
 */
extern const uint8_t ArialMT_Plain_10_CS[] PROGMEM;
extern const uint8_t ArialMT_Plain_16_CS[] PROGMEM;
extern const uint8_t ArialMT_Plain_24_CS[] PROGMEM;
#endif