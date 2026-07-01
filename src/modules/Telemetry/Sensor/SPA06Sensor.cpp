#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && __has_include(<Adafruit_SPA06_003.h>)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "SPA06Sensor.h"
#include "TelemetrySensor.h"
#include <Adafruit_SPA06_003.h>

SPA06Sensor::SPA06Sensor() : TelemetrySensor(meshtastic_TelemetrySensorType_SPA06, "SPA06") {}

bool SPA06Sensor::initDevice(TwoWire *bus, ScanI2C::FoundDevice *dev)
{
    LOG_INFO("Init sensor: %s", sensorName);
    status = spa06.begin(dev->address.address, bus);
    if (!status) {
        return status;
    }
    // Set moderate precision for faster sampling
    spa06.setPressureOversampling(SPA06_003_OVERSAMPLE_8);    // 8x oversampling
    spa06.setTemperatureOversampling(SPA06_003_OVERSAMPLE_8); // 8x oversampling

    // Set measurement rate
    spa06.setPressureMeasureRate(SPA06_003_RATE_32);    // 32 Hz
    spa06.setTemperatureMeasureRate(SPA06_003_RATE_32); // 32 Hz
    spa06.setMeasurementMode(SPA06_003_MEAS_CONTINUOUS_BOTH);

    spa_temp = spa06.getTemperatureSensor();
    spa_pressure = spa06.getPressureSensor();

    initI2CSensor();
    return status;
}

bool SPA06Sensor::getMetrics(meshtastic_Telemetry *measurement)
{
    sensors_event_t temp, press;

    if (!spa_temp->getEvent(&temp) || !spa_pressure->getEvent(&press)) {
        LOG_DEBUG("SPA06 getEvents no data");
        return false;
    }

    measurement->variant.environment_metrics.has_temperature = true;
    measurement->variant.environment_metrics.has_barometric_pressure = true;
    measurement->variant.environment_metrics.temperature = temp.temperature;
    measurement->variant.environment_metrics.barometric_pressure = press.pressure;

    return true;
}
#endif
