/*
 *  Worth noting that both the AHT10 and AHT20 are supported without alteration.
 */

#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && __has_include(<Adafruit_AHTX0.h>)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "AHT10.h"
#include "TelemetrySensor.h"

#include <Adafruit_AHTX0.h>
#include <typeinfo>

AHT10Sensor::AHT10Sensor() : TelemetrySensor(meshtastic_TelemetrySensorType_AHT10, "AHT10") {}

bool AHT10Sensor::initDevice(TwoWire *bus, ScanI2C::FoundDevice *dev)
{
    LOG_INFO("Init sensor: %s", sensorName);
    aht10 = Adafruit_AHTX0();
    status = aht10.begin(bus, 0, dev->address.address);

    initI2CSensor();
    return status;
}

bool AHT10Sensor::getMetrics(meshtastic_Telemetry *measurement)
{
    LOG_DEBUG("AHT10 getMetrics");

    sensors_event_t humidity, temp;
    aht10.getEvent(&humidity, &temp);

    // prefer other sensors like bmp280, bmp3xx
    if (!measurement->variant.environment_metrics.has_temperature) {
        measurement->variant.environment_metrics.has_temperature = true;
        measurement->variant.environment_metrics.temperature = temp.temperature;
    }

    if (!measurement->variant.environment_metrics.has_relative_humidity) {
        measurement->variant.environment_metrics.has_relative_humidity = true;
        measurement->variant.environment_metrics.relative_humidity = humidity.relative_humidity;
    }

    return true;
}

#endif
