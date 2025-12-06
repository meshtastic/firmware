#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && __has_include(<Adafruit_PCT2075.h>)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "PCT2075Sensor.h"
#include "TelemetrySensor.h"
#include <Adafruit_PCT2075.h>

PCT2075Sensor::PCT2075Sensor() : TelemetrySensor(meshtastic_TelemetrySensorType_PCT2075, "PCT2075") {}

bool PCT2075Sensor::initDevice(TwoWire *bus, ScanI2C::FoundDevice *dev)
{
    LOG_INFO("Init sensor: %s", sensorName);
    status = pct2075.begin(dev->address.address, bus);

    initI2CSensor();
    return status;
}

bool PCT2075Sensor::getMetrics(meshtastic_Telemetry *measurement)
{
    measurement->variant.environment_metrics.has_temperature = true;
    measurement->variant.environment_metrics.temperature = pct2075.getTemperature();

    return true;
}

#endif
