/*
 *  Support for the ClimateGuard RadSens Dosimeter
 *  A fun and educational sensor for Meshtastic; not for safety critical applications.
 */
#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "CGRadSensSensor.h"
#include "TelemetrySensor.h"
#include <Wire.h>
#include <typeinfo>

CGRadSensSensor::CGRadSensSensor() : TelemetrySensor(meshtastic_TelemetrySensorType_RADSENS, "RadSens") {}

bool CGRadSensSensor::initDevice(TwoWire *bus, ScanI2C::FoundDevice *dev)
{
    // Initialize the sensor following the same pattern as RCWL9620Sensor
    LOG_INFO("Init sensor: %s", sensorName);
    status = true;
    begin(bus, dev->address.address);
    initI2CSensor();
    return status;
}

void CGRadSensSensor::begin(TwoWire *wire, uint8_t addr)
{
    // Store the Wire and address to the sensor following the same pattern as RCWL9620Sensor
    _wire = wire;
    _addr = addr;
    _wire->begin();
}

bool CGRadSensSensor::getStaticRadiation(float &microRadPerHr)
{
    // Register 0x06..0x08 is unsigned 24-bit BE dose rate in 0.1 µR/h.
    // Slave may NACK/timeout once under clock stretch; retry before giving up.
    for (int attempt = 0; attempt < 3; attempt++) {
        _wire->beginTransmission(_addr);
        _wire->write(0x06);
        if (_wire->endTransmission() != 0) {
            continue;
        }
        if (_wire->requestFrom(_addr, (uint8_t)3) != 3) {
            continue;
        }
        uint8_t b0 = (uint8_t)_wire->read();
        uint8_t b1 = (uint8_t)_wire->read();
        uint8_t b2 = (uint8_t)_wire->read();
        uint32_t value = ((uint32_t)b0 << 16) | ((uint32_t)b1 << 8) | b2;
        microRadPerHr = float(value) / 10.0f;
        return true;
    }
    return false;
}

bool CGRadSensSensor::getMetrics(meshtastic_Telemetry *measurement)
{
    LOG_DEBUG("CGRADSENS getMetrics");
    float microRadPerHr = 0.0f;
    if (!getStaticRadiation(microRadPerHr)) {
        return false;
    }
    measurement->variant.environment_metrics.has_radiation = true;
    measurement->variant.environment_metrics.radiation = microRadPerHr;
    return true;
}
#endif
