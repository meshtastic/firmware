#include "../mesh/generated/telemetry.pb.h"
#include "configuration.h"
#include "TelemetrySensor.h"
#include "LPS22HBSensor.h"
#include <Adafruit_LPS2X.h>
#include <Adafruit_Sensor.h>

LPS22HBSensor::LPS22HBSensor() : 
    TelemetrySensor(TelemetrySensorType_LPS22, "LPS22HB") 
{
}

int32_t LPS22HBSensor::runOnce() {
    DEBUG_MSG("Init sensor: %s\n", sensorName);
    if (!hasSensor()) {
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    }
    status = lps22hb.begin_I2C(nodeTelemetrySensorsMap[sensorType]);
    return initI2CSensor();
}

void LPS22HBSensor::setup() 
{
    lps22hb.setDataRate(LPS22_RATE_10_HZ);
}

bool LPS22HBSensor::getMetrics(Telemetry *measurement) {
    sensors_event_t temp;
    sensors_event_t pressure;
    lps22hb.getEvent(&pressure, &temp);

    measurement->variant.environment_metrics.temperature = temp.temperature;
    measurement->variant.environment_metrics.barometric_pressure = pressure.pressure;

    return true;
}    