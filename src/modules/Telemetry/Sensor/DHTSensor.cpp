#include "DHTSensor.h"
#include "./mesh/generated/telemetry.pb.h"
#include "MeshService.h"
#include "TelemetrySensor.h"
#include "configuration.h"
#include <DHT.h>

DHTSensor::DHTSensor() : 
    TelemetrySensor(TelemetrySensorType_NotSet, "DHT") 
{
}

int32_t DHTSensor::runOnce() {
    if (moduleConfig.telemetry.environment_sensor_type == TelemetrySensorType_DHT11 || 
        moduleConfig.telemetry.environment_sensor_type == TelemetrySensorType_DHT12) {
        dht = new DHT(moduleConfig.telemetry.environment_sensor_pin, DHT11);
    } else {
        dht = new DHT(moduleConfig.telemetry.environment_sensor_pin, DHT22);
    }

    dht->begin();
    dht->read();
    DEBUG_MSG("Opened DHT11/DHT12 on pin: %d\n", moduleConfig.telemetry.environment_sensor_pin);

    return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
}

void DHTSensor::setup() { }

bool DHTSensor::getMetrics(Telemetry *measurement) {
    DEBUG_MSG("DHTSensor::getMetrics\n");
    if (!dht->read(true)) {
        DEBUG_MSG("Telemetry: FAILED TO READ DATA\n");
        return false;
    }
    measurement->variant.environment_metrics.relative_humidity = dht->readHumidity();
    measurement->variant.environment_metrics.temperature = dht->readTemperature();
    return true;
}