#include "DallasSensor.h"
#include "../mesh/generated/telemetry.pb.h"
#include "MeshService.h"
#include "TelemetrySensor.h"
#include "configuration.h"
#include <DS18B20.h>
#include <OneWire.h>

DallasSensor::DallasSensor() : 
    TelemetrySensor(TelemetrySensorType_DS18B20, "DS18B20") 
{
}

int32_t DallasSensor::runOnce() {
    oneWire = new OneWire(moduleConfig.telemetry.environment_sensor_pin);
    ds18b20 = new DS18B20(oneWire);
    ds18b20->begin();
    ds18b20->setResolution(12);
    ds18b20->requestTemperatures();
    DEBUG_MSG("Opened DS18B20 on pin: %d\n", moduleConfig.telemetry.environment_sensor_pin);
    return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
}

void DallasSensor::setup() {}

bool DallasSensor::getMetrics(Telemetry *measurement){
    DEBUG_MSG("DallasSensor::getMetrics\n");
    if (ds18b20->isConversionComplete()) {
        measurement->variant.environment_metrics.temperature = ds18b20->getTempC();
        measurement->variant.environment_metrics.relative_humidity = 0;
        ds18b20->requestTemperatures();
        return true;
    }
    return false;
}