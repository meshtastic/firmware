#include "RCWL9620Sensor.h"
#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "TelemetrySensor.h"
#include "configuration.h"

RCWL9620Sensor::RCWL9620Sensor() : TelemetrySensor(meshtastic_TelemetrySensorType_RCWL9620, "RCWL9620") {}

int32_t RCWL9620Sensor::runOnce()
{
    LOG_INFO("Init sensor: %s\n", sensorName);
    if (!hasSensor()) {
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    }
    status = begin(nodeTelemetrySensorsMap[sensorType].first, nodeTelemetrySensorsMap[sensorType].second);
    return initI2CSensor();
}

void RCWL9620Sensor::setup() {}

bool RCWL9620Sensor::getMetrics(meshtastic_Telemetry *measurement)
{
    LOG_DEBUG("RCWL9620Sensor::getMetrics\n");
    measurement->variant.environment_metrics.distance = getDistance();
    return true;
}

bool RCWL9620Sensor::begin(uint8_t addr, TwoWire *wire)
{
    _wire = wire;
    _addr = addr;
    if (i2c_dev)
        delete i2c_dev;
    i2c_dev = new Adafruit_I2CDevice(_addr, _wire);
    if (!i2c_dev->begin())
        return false;
    return true;
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