#include "../mesh/generated/telemetry.pb.h"
#include "configuration.h"
#include "TelemetrySensor.h"
#include "INA219Sensor.h"
#include <Adafruit_INA219.h>

INA219Sensor::INA219Sensor() : 
    TelemetrySensor(TelemetrySensorType_INA219, "INA219") 
{
}

int32_t INA219Sensor::runOnce() {
    DEBUG_MSG("Init sensor: %s\n", sensorName);
    if (!hasSensor()) {
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    }
    ina219 = Adafruit_INA219(nodeTelemetrySensorsMap[sensorType]);
    status = ina219.begin();
    return initI2CSensor();
}

void INA219Sensor::setup() 
{
}

bool INA219Sensor::getMetrics(Telemetry *measurement) {
    measurement->variant.environment_metrics.voltage = ina219.getBusVoltage_V();
    measurement->variant.environment_metrics.current = ina219.getCurrent_mA();
    return true;
}    