#include "configuration.h"

#ifndef BME680_HEADER
#if defined(ARCH_PORTDUINO)
#define BME680_HEADER <Adafruit_BME680.h>
#else
#define BME680_HEADER <bsec2.h>
#endif
#endif
