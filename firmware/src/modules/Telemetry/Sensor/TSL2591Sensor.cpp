#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && __has_include(<Adafruit_TSL2591.h>)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "TSL2591Sensor.h"
#include "TelemetrySensor.h"
#include <Adafruit_TSL2591.h>
#include <typeinfo>

TSL2591Sensor::TSL2591Sensor() : TelemetrySensor(meshtastic_TelemetrySensorType_TSL25911FN, "TSL2591") {}

bool TSL2591Sensor::initDevice(TwoWire *bus, ScanI2C::FoundDevice *dev)
{
    LOG_INFO("Init sensor: %s", sensorName);
    status = tsl.begin(bus);
    if (!status) {
        return status;
    }
    tsl.setGain(TSL2591_GAIN_LOW); // 1x gain
    tsl.setTiming(TSL2591_INTEGRATIONTIME_100MS);

    initI2CSensor();
    return status;
}

bool TSL2591Sensor::getMetrics(meshtastic_Telemetry *measurement)
{
    measurement->variant.environment_metrics.has_lux = true;
    uint32_t lum = tsl.getFullLuminosity();
    uint16_t ir, full;
    ir = lum >> 16;
    full = lum & 0xFFFF;

    measurement->variant.environment_metrics.lux = tsl.calculateLux(full, ir);
    LOG_INFO("Lux: %f", measurement->variant.environment_metrics.lux);

    return true;
}

#endif
