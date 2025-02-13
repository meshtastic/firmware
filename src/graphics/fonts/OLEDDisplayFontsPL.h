#ifndef OLEDDISPLAYFONTSPL_h
#define OLEDDISPLAYFONTSPL_h

#ifdef ARDUINO
#include <Arduino.h>
#elif __MBED__
#define PROGMEM
#endif
extern const uint8_t ArialMT_Plain_10_PL[] PROGMEM;
extern const uint8_t ArialMT_Plain_16_PL[] PROGMEM;
extern const uint8_t ArialMT_Plain_24_PL[] PROGMEM;
#endif