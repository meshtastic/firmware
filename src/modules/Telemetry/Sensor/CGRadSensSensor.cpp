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

float CGRadSensSensor::getStaticRadiation()
{
    // Read a register, following the same pattern as the RCWL9620Sensor
    _wire->beginTransmission(_addr); // Transfer data to addr.
    _wire->write(0x06);              // Radiation intensity (static period T = 500 sec)
    if (_wire->endTransmission() == 0) {
        if (_wire->requestFrom(_addr, (uint8_t)3)) {
            ; // Request 3 bytes
            uint32_t data = _wire->read();
            data <<= 8;
            data |= _wire->read();
            data <<= 8;
            data |= _wire->read();

            // As per the data sheet for the RadSens
            // Register 0x06 contains the reading in 0.1 * μR / h
            float microRadPerHr = float(data) / 10.0;
            return microRadPerHr;
        }
    }
    return -1.0;
}

bool CGRadSensSensor::getMetrics(meshtastic_Telemetry *measurement)
{
    // Store the meansurement in the the appropriate fields of the protobuf
    measurement->variant.environment_metrics.has_radiation = true;

    LOG_DEBUG("CGRADSENS getMetrics");
    measurement->variant.environment_metrics.radiation = getStaticRadiation();

    return true;
}
#endif