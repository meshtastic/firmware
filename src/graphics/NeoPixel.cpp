#include "configuration.h"

#ifdef HAS_NEOPIXEL
#include "graphics/NeoPixel.h"

Adafruit_NeoPixel pixels(NEOPIXEL_COUNT, NEOPIXEL_DATA, NEOPIXEL_TYPE);
#endif
