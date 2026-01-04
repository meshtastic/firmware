#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && __has_include(<BH1750_WE.h>)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "BH1750Sensor.h"
#include "TelemetrySensor.h"
#include <BH1750_WE.h>

#ifndef BH1750_SENSOR_MODE
#define BH1750_SENSOR_MODE BH1750Mode::CHM
#endif

BH1750Sensor::BH1750Sensor() : TelemetrySensor(meshtastic_TelemetrySensorType_BH1750, "BH1750") {}

bool BH1750Sensor::initDevice(TwoWire *bus, ScanI2C::FoundDevice *dev)
{
    LOG_INFO("Init sensor: %s with mode %d", sensorName, BH1750_SENSOR_MODE);

    bh1750 = BH1750_WE(bus, dev->address.address);
    status = bh1750.init();
    if (!status) {
        return status;
    }

    bh1750.setMode(BH1750_SENSOR_MODE);

    initI2CSensor();
    return status;
}

bool BH1750Sensor::getMetrics(meshtastic_Telemetry *measurement)
{

    /* An OTH and OTH_2 measurement takes ~120 ms. I suggest to wait
    140 ms to be on the safe side.
    An OTL measurement takes about 16 ms. I suggest to wait 20 ms
    to be on the safe side. */
    if (BH1750_SENSOR_MODE == BH1750Mode::OTH || BH1750_SENSOR_MODE == BH1750Mode::OTH_2) {
        bh1750.setMode(BH1750_SENSOR_MODE);
        delay(140); // wait for measurement to be completed
    } else if (BH1750_SENSOR_MODE == BH1750Mode::OTL) {
        bh1750.setMode(BH1750_SENSOR_MODE);
        delay(20);
    }

    measurement->variant.environment_metrics.has_lux = true;
    float lightIntensity = bh1750.getLux();

    measurement->variant.environment_metrics.lux = lightIntensity;
    return true;
}

#endif
