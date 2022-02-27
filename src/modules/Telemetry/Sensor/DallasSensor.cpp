#include "../mesh/generated/telemetry.pb.h"
#include "configuration.h"
#include "MeshService.h"
#include "TelemetrySensor.h"
#include "DallasSensor.h"
#include <DS18B20.h>
#include <OneWire.h>

DallasSensor::DallasSensor() : TelemetrySensor {} {
}

int32_t DallasSensor::runOnce() {
    oneWire = new OneWire(radioConfig.preferences.telemetry_module_sensor_pin);
    ds18b20 = new DS18B20(oneWire);
    ds18b20->begin();
    ds18b20->setResolution(12);
    ds18b20->requestTemperatures();
    DEBUG_MSG("Telemetry: Opened DS18B20 on pin: %d\n",
                radioConfig.preferences.telemetry_module_sensor_pin);
    return (DS18B20_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS);
}

bool DallasSensor::getMeasurement(Telemetry *measurement) {
    if (ds18b20->isConversionComplete()) {
        measurement->temperature = ds18b20->getTempC();
        measurement->relative_humidity = 0;
        ds18b20->requestTemperatures();
        return true;
    } 
    return false;
}