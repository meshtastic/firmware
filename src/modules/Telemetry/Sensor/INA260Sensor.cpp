#include "../mesh/generated/telemetry.pb.h"
#include "configuration.h"
#include "TelemetrySensor.h"
#include "INA260Sensor.h"
#include <Adafruit_INA260.h>

INA260Sensor::INA260Sensor() : 
    TelemetrySensor(TelemetrySensorType_INA260, "INA260") 
{
}

int32_t INA260Sensor::runOnce() {
    DEBUG_MSG("Init sensor: %s\n", sensorName);
    if (!hasSensor()) {
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    }
    if(i2cScanMap[nodeTelemetrySensorsMap[sensorType]].bus == 1) {
#ifdef I2C_SDA1
        status = ina260.begin(nodeTelemetrySensorsMap[sensorType], &Wire1);
#endif
    } else {
        status = ina260.begin(nodeTelemetrySensorsMap[sensorType], &Wire);
    }
    return initI2CSensor();
}

void INA260Sensor::setup() 
{
}

bool INA260Sensor::getMetrics(Telemetry *measurement) {
    // mV conversion to V
    measurement->variant.environment_metrics.voltage = ina260.readBusVoltage() / 1000;
    measurement->variant.environment_metrics.current = ina260.readCurrent();
    return true;
}    