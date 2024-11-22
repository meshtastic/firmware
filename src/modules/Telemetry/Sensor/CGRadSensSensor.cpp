#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "CGRadSensSensor.h"
#include "TelemetrySensor.h"
#include <typeinfo>
#include <Wire.h>

CGRadSensSensor::CGRadSensSensor() : TelemetrySensor(meshtastic_TelemetrySensorType_RADSENS, "RadSens") {}

int32_t CGRadSensSensor::runOnce()
{
    LOG_INFO("Init sensor: %s", sensorName);
    if (!hasSensor()) {
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    }

    status = true;
    begin(nodeTelemetrySensorsMap[sensorType].second, nodeTelemetrySensorsMap[sensorType].first);

    return initI2CSensor();
}

void CGRadSensSensor::setup() {}

void CGRadSensSensor::begin(TwoWire *wire, uint8_t addr)
{
    _wire = wire;
    _addr = addr;
    _wire->begin();
}

float CGRadSensSensor::getStaticRadiation()
{
    uint32_t data;
    _wire->beginTransmission(_addr); // Transfer data to addr.
    _wire->write(0x06);              // Radiation intensity (static period T = 500 sec)
    if (_wire->endTransmission() == 0) {
       if (_wire->requestFrom(_addr, (uint8_t)3)) { ; // Request 3 bytes
            data = _wire->read();
            data <<= 8;
            data |= _wire->read();
            data <<= 8;
            data |= _wire->read();

            float microRadPerHr = float(data) / 10.0;
            return microRadPerHr;
        } 
    }
    return -1.0;
}

bool CGRadSensSensor::getMetrics(meshtastic_Telemetry *measurement)
{
    LOG_INFO("getMetrics: ClimateGuard RadSense Geiger-Muller Sensor");
    measurement->variant.environment_metrics.has_radiation = true;

    LOG_DEBUG("CGRADSENS getMetrics");
    measurement->variant.environment_metrics.radiation = getStaticRadiation();

    return true;
}
#endif