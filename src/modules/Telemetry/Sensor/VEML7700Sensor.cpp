#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && __has_include(<Adafruit_VEML7700.h>)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "TelemetrySensor.h"
#include "VEML7700Sensor.h"

#include <Adafruit_VEML7700.h>
#include <typeinfo>

VEML7700Sensor::VEML7700Sensor() : TelemetrySensor(meshtastic_TelemetrySensorType_VEML7700, "VEML7700") {}

int32_t VEML7700Sensor::runOnce()
{
    LOG_INFO("Init sensor: %s", sensorName);
    if (!hasSensor()) {
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    }
    status = veml7700.begin(nodeTelemetrySensorsMap[sensorType].second);

    veml7700.setLowThreshold(10000);
    veml7700.setHighThreshold(20000);
    veml7700.interruptEnable(true);

    return initI2CSensor();
}

void VEML7700Sensor::setup() {}

/*!
 *    @brief Copmute lux from ALS reading.
 *    @param rawALS raw ALS register value
 *    @param corrected if true, apply non-linear correction
 *    @return lux value
 */
float VEML7700Sensor::computeLux(uint16_t rawALS, bool corrected)
{
    float lux = getResolution() * rawALS;
    if (corrected)
        lux = (((6.0135e-13 * lux - 9.3924e-9) * lux + 8.1488e-5) * lux + 1.0023) * lux;
    return lux;
}

/*!
 *    @brief Determines resolution for current gain and integration time
 * settings.
 */
float VEML7700Sensor::getResolution(void)
{
    return MAX_RES * (IT_MAX / veml7700.getIntegrationTimeValue()) * (GAIN_MAX / veml7700.getGainValue());
}

bool VEML7700Sensor::getMetrics(meshtastic_Telemetry *measurement)
{
    measurement->variant.environment_metrics.has_lux = true;
    measurement->variant.environment_metrics.has_white_lux = true;

    int16_t white;
    measurement->variant.environment_metrics.lux = veml7700.readLux(VEML_LUX_AUTO);
    white = veml7700.readWhite(true);
    measurement->variant.environment_metrics.white_lux = computeLux(white, white > 100);
    LOG_INFO("white lux %f, als lux %f", measurement->variant.environment_metrics.white_lux,
             measurement->variant.environment_metrics.lux);

    return true;
}
#endif