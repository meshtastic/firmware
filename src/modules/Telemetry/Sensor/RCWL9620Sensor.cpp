#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "RCWL9620Sensor.h"
#include "TelemetrySensor.h"

RCWL9620Sensor::RCWL9620Sensor() : TelemetrySensor(meshtastic_TelemetrySensorType_RCWL9620, "RCWL9620") {}

int32_t RCWL9620Sensor::runOnce()
{
    LOG_INFO("Init sensor: %s", sensorName);
    if (!hasSensor()) {
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    }
    status = 1;
    begin(nodeTelemetrySensorsMap[sensorType].second, nodeTelemetrySensorsMap[sensorType].first);
    return initI2CSensor();
}

void RCWL9620Sensor::setup() {}

bool RCWL9620Sensor::getMetrics(meshtastic_Telemetry *measurement)
{
    measurement->variant.environment_metrics.has_distance = true;
    LOG_DEBUG("RCWL9620 getMetrics");
    measurement->variant.environment_metrics.distance = getDistance();
    return true;
}

void RCWL9620Sensor::begin(TwoWire *wire, uint8_t addr, uint8_t sda, uint8_t scl, uint32_t speed)
{
    _wire = wire;
    _addr = addr;
    _sda = sda;
    _scl = scl;
    _speed = speed;
    _wire->begin();
}

float RCWL9620Sensor::getDistance()
{
    uint32_t data;
    _wire->beginTransmission(_addr); // Transfer data to addr.
    _wire->write(0x01);
    _wire->endTransmission(); // Stop data transmission with the Ultrasonic
                              // Unit.

    _wire->requestFrom(_addr,
                       (uint8_t)3); // Request 3 bytes from Ultrasonic Unit.

    data = _wire->read();
    data <<= 8;
    data |= _wire->read();
    data <<= 8;
    data |= _wire->read();
    float Distance = float(data) / 1000;
    if (Distance > 4500.00) {
        return 4500.00;
    } else {
        return Distance;
    }
}

#endif