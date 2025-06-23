#include "configuration.h"

#if HAS_TELEMETRY && !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && __has_include(<Adafruit_ADS1015.h>)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "ADS1X15Sensor.h"
#include "TelemetrySensor.h"
#include <Adafruit_ADS1015.h>

ADS1X15Sensor::ADS1X15Sensor() : TelemetrySensor(meshtastic_TelemetrySensorType_ADS1X15, "ADS1X15") {}

int32_t ADS1X15Sensor::runOnce()
{
    LOG_INFO("Init sensor: %s", sensorName);
    if (!hasSensor()) {
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    }

    status = ads1x15.begin(nodeTelemetrySensorsMap[sensorType].first);

    return initI2CSensor();
}

void ADS1X15Sensor::setup() {}

struct _ADS1X15Measurement ADS1X15Sensor::getMeasurement(ads1x15_ch_t ch)
{
    struct _ADS1X15Measurement measurement;

    // Reset gain
    ads1x15.setGain(GAIN_TWOTHIRDS);
    double voltage_range = 6.144;

    // Get value with full range
    uint16_t value = ads.readADC_SingleEnded(ch);

    // Dynamic gain, to increase resolution of low voltage values
    // If value is under 4.096v increase the gain depending on voltage
    if (value < 21845) {
        if (value > 10922) {

            // 1x gain, 4.096V
            ads1x15.setGain(GAIN_ONE);
            voltage_range = 4.096;

        } else if (value > 5461) {

            // 2x gain, 2.048V
            ads1x15.setGain(GAIN_TWO);
            voltage_range = 2.048;

        } else if (value > 2730) {

            // 4x gain, 1.024V
            ads1x15.setGain(GAIN_FOUR);
            voltage_range = 1.024;

        } else if (value > 1365) {

            // 8x gain, 0.25V
            ads1x15.setGain(GAIN_EIGHT);
            voltage_range = 0.512;

        } else {

            // 16x gain, 0.125V
            ads1x15.setGain(GAIN_SIXTEEN);
            voltage_range = 0.256;
        }

        // Get the value again
        value = ads1x15.readADC_SingleEnded(ch);
    }

    reading = (float)value / 32768 * voltage_range;
    measurement.voltage = reading;

    return measurement;
}

struct _ADS1X15Measurements ADS1X15Sensor::getMeasurements()
{
    struct _ADS1X15Measurements measurements;

    // ADS1X15 has 4 channels starting from 0
    for (int i = 0; i < 4; i++) {
        measurements.measurements[i] = getMeasurement((ads1x15_ch_t)i);
    }

    return measurements;
}

bool ADS1X15Sensor::getMetrics(meshtastic_Telemetry *measurement)
{

    struct _ADS1X15Measurements m = getMeasurements();

    measurement->variant.power_metrics.has_ch1_voltage = true;
    measurement->variant.power_metrics.has_ch2_voltage = true;
    measurement->variant.power_metrics.has_ch3_voltage = true;
    measurement->variant.power_metrics.has_ch4_voltage = true;

    measurement->variant.power_metrics.ch1_voltage = m.measurements[0].voltage;
    measurement->variant.power_metrics.ch2_voltage = m.measurements[1].voltage;
    measurement->variant.power_metrics.ch3_voltage = m.measurements[2].voltage;
    measurement->variant.power_metrics.ch4_voltage = m.measurements[3].voltage;

    return true;
}

#endif