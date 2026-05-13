#include "station_common.h"

#ifdef USE_SX1262
// Station G3 reuses the same Fast-Transient DC-DC PA design as G2 (BQ35LORA900V1M).
// PA Operating Mode is set in hardware via the PA-PL1 / PA-PL2 jumpers.
// 19 matches the G2 cap (SX1262 19 dBm in → ~31 dBm PA out at Power Level 1, ISM compliant).
// Raise (max 22) only if running a higher PA Power Level and you can stay within local band limits.
#define SX126X_MAX_POWER 19
#endif

/*
#define BATTERY_PIN 4 // A battery voltage measurement pin, voltage divider connected here to measure battery voltage
#define ADC_CHANNEL ADC1_GPIO4_CHANNEL
#define ADC_MULTIPLIER 4
#define BATTERY_SENSE_SAMPLES 15 // Set the number of samples, It has an effect of increasing sensitivity.
#define BAT_FULLVOLT 8400
#define BAT_EMPTYVOLT 5000
#define BAT_CHARGINGVOLT 8400
#define BAT_NOBATVOLT 4460
#define CELL_TYPE_LION // same curve for liion/lipo
#define NUM_CELLS 2
*/
