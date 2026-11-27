#ifndef OLEDDISPLAYFONTSTINY_h
#define OLEDDISPLAYFONTSTINY_h

#ifdef ARDUINO
#include <Arduino.h>
#elif __MBED__
#define PROGMEM
#endif

/**
 * 3x5 pixel font (6 px line) for compact OLED panels, printable ASCII only.
 */
extern const uint8_t TomThumb_6[] PROGMEM;
#endif
