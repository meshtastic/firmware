#include "VEML7700Sensor.h"
#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "TelemetrySensor.h"
#include "configuration.h"
#include <Adafruit_VEML7700.h>
#include <typeinfo>

VEML7700Sensor::VEML7700Sensor() : TelemetrySensor(meshtastic_TelemetrySensorType_VEML7700, "VEML7700") {}

int32_t VEML7700Sensor::runOnce()
{
    LOG_INFO("Init sensor: %s\n", sensorName);
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

bool VEML7700Sensor::getMetrics(meshtastic_Telemetry *measurement)
{
    measurement->variant.environment_metrics.lux = veml7700.readLux(VEML_LUX_AUTO);
    measurement->variant.environment_metrics.white = veml7700.readWhite();
    measurement->variant.environment_metrics.ALS = veml7700.readALS();


    return true;
}