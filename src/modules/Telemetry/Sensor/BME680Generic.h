#include "configuration.h"

#if !defined(BME680_BSEC2_SUPPORTED)
#if defined(RAK_4631)
#define BME680_BSEC2_SUPPORTED 1
#define BME680_HEADER <bsec2.h>
#else
#define BME680_BSEC2_SUPPORTED 0
#define BME680_HEADER <Adafruit_BME680.h>
#endif // defined(RAK_4631)
#endif // !defined(BME680_BSEC2_SUPPORTED)
