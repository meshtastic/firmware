#include "DHTSensor.h"
#include "./mesh/generated/telemetry.pb.h"
#include "MeshService.h"
#include "TelemetrySensor.h"
#include "configuration.h"
#include <DHT.h>

DHTSensor::DHTSensor() : TelemetrySensor{} {}

int32_t DHTSensor::runOnce()
{
    if (TelemetrySensorType_DHT11 || TelemetrySensorType_DHT12) {
        dht = new DHT(moduleConfig.telemetry.environment_sensor_pin, DHT11);
    } else {
        dht = new DHT(moduleConfig.telemetry.environment_sensor_pin, DHT22);
    }

    dht->begin();
    dht->read();
    DEBUG_MSG("Telemetry: Opened DHT11/DHT12 on pin: %d\n", moduleConfig.telemetry.environment_sensor_pin);

    return (DHT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS);
}

bool DHTSensor::getMeasurement(Telemetry *measurement)
{
    if (!dht->read(true)) {
        DEBUG_MSG("Telemetry: FAILED TO READ DATA\n");
        return false;
    }
    measurement->variant.environment_metrics.relative_humidity = dht->readHumidity();
    measurement->variant.environment_metrics.temperature = dht->readTemperature();
    return true;
}