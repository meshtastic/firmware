#include "configuration.h"

#if HAS_TELEMETRY && !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR 

#include "DS18B20Sensor.h"
#include "TelemetrySensor.h"
#include <typeinfo>
#include <DallasTemperature.h>
#include "../mesh/generated/meshtastic/telemetry.pb.h"

DS18B20Sensor::DS18B20Sensor() : TelemetrySensor(meshtastic_TelemetrySensorType_DS18B20, "DS18B20") {
    pin = PIN_WIRE_DS18B20; // Define this pin in your board's variant.h
    oneWire = new OneWire(pin);
    sensors = new DallasTemperature(oneWire);
    sensors->begin();
    status = sensors->getDeviceCount() > 0;
}

int32_t DS18B20Sensor::runOnce()
{
    if (!hasSensor()) {
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    }
    sensors->requestTemperatures();
    status = sensors->getDeviceCount() > 0;
    LOG_INFO("DS18B20: pin=%d, device_count=%d", pin, sensors->getDeviceCount());
    return initI2CSensor();
}

void DS18B20Sensor::setup() {}

bool DS18B20Sensor::getMetrics(meshtastic_Telemetry *measurement)
{
    if (!hasSensor()) return false;

    sensors->requestTemperatures();
    float tempC = sensors->getTempCByIndex(0);

    if(tempC != DEVICE_DISCONNECTED_C) {
        measurement->variant.environment_metrics.has_temperature = true;
        measurement->variant.environment_metrics.temperature = tempC;
        return true;
    }
    return false;
}
#endif