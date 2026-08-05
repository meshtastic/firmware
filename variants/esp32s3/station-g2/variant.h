/*
Board Information: https://wiki.uniteng.com/en/meshtastic/station-g2
*/

#include "station_common.h"

#ifdef USE_SX1262
// Ensure the PA does not exceed the saturation output power. More
// Info:https://wiki.uniteng.com/en/meshtastic/station-g2#summary-for-lora-power-amplifier-conduction-test
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
