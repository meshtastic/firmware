#include "../mesh/generated/telemetry.pb.h"
#include "configuration.h"
#include "MeshService.h"
#include "TelemetrySensor.h"
#include "DHTSensor.h"
#include <DHT.h>

DHTSensor::DHTSensor() : TelemetrySensor {} {
}

int32_t DHTSensor::runOnce() {
    if (RadioConfig_UserPreferences_TelemetrySensorType_DHT11 || 
        RadioConfig_UserPreferences_TelemetrySensorType_DHT12) {
        dht = new DHT(radioConfig.preferences.telemetry_module_environment_sensor_pin, DHT11);
    }
    else { 
        dht = new DHT(radioConfig.preferences.telemetry_module_environment_sensor_pin, DHT22);
    }

    dht->begin();
    dht->read();
    DEBUG_MSG("Telemetry: Opened DHT11/DHT12 on pin: %d\n",
                radioConfig.preferences.telemetry_module_environment_sensor_pin);

    return (DHT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS);
}

bool DHTSensor::getMeasurement(Telemetry *measurement) {
    if (!dht->read(true)) {
        DEBUG_MSG("Telemetry: FAILED TO READ DATA\n");
        return false;
    }
    measurement->variant.environment_metrics.relative_humidity = dht->readHumidity();
    measurement->variant.environment_metrics.temperature = dht->readTemperature();
    return true;
}    