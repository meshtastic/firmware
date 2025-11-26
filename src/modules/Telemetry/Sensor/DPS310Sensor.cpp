#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && __has_include(<Adafruit_DPS310.h>)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "DPS310Sensor.h"
#include "TelemetrySensor.h"
#include <Adafruit_DPS310.h>

DPS310Sensor::DPS310Sensor() : TelemetrySensor(meshtastic_TelemetrySensorType_DPS310, "DPS310") {}

bool DPS310Sensor::initDevice(TwoWire *bus, ScanI2C::FoundDevice *dev)
{
    LOG_INFO("Init sensor: %s", sensorName);
    status = dps310.begin_I2C(dev->address.address, bus);
    if (!status) {
        return status;
    }

    dps310.configurePressure(DPS310_1HZ, DPS310_4SAMPLES);
    dps310.configureTemperature(DPS310_1HZ, DPS310_4SAMPLES);
    dps310.setMode(DPS310_CONT_PRESTEMP);

    initI2CSensor();
    return status;
}

bool DPS310Sensor::getMetrics(meshtastic_Telemetry *measurement)
{
    sensors_event_t temp, press;

    if (!dps310.getEvents(&temp, &press)) {
        LOG_DEBUG("DPS310 getEvents no data");
        return false;
    }

    measurement->variant.environment_metrics.has_temperature = true;
    measurement->variant.environment_metrics.has_barometric_pressure = true;
    measurement->variant.environment_metrics.temperature = temp.temperature;
    measurement->variant.environment_metrics.barometric_pressure = press.pressure;

    return true;
}
#endif