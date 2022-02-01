#include "../mesh/generated/environmental_measurement.pb.h"
#include "configuration.h"
#include "MeshService.h"
#include "EnvironmentalMeasurementSensor.h"
#include "DHTSensor.h"
#include <DHT.h>

DHTSensor::DHTSensor() : EnvironmentalMeasurementSensor {} {
}

int32_t DHTSensor::runOnce() {
    if (RadioConfig_UserPreferences_EnvironmentalMeasurementSensorType_DHT11 || 
        RadioConfig_UserPreferences_EnvironmentalMeasurementSensorType_DHT12) {
        dht = new DHT(radioConfig.preferences.environmental_measurement_plugin_sensor_pin, DHT11);
    }
    else { 
        dht = new DHT(radioConfig.preferences.environmental_measurement_plugin_sensor_pin, DHT22);
    }

    dht->begin();
    dht->read();
    DEBUG_MSG("EnvironmentalMeasurement: Opened DHT11/DHT12 on pin: %d\n",
                radioConfig.preferences.environmental_measurement_plugin_sensor_pin);

    return (DHT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS);
}

bool DHTSensor::getMeasurement(EnvironmentalMeasurement *measurement) {
    if (!dht->read(true)) {
        DEBUG_MSG("EnvironmentalMeasurement: FAILED TO READ DATA\n");
        return false;
    }
    measurement->relative_humidity = dht->readHumidity();
    measurement->temperature = dht->readTemperature();
    return true;
}    