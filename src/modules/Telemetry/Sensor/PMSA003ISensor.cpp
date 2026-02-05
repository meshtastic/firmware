#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_AIR_QUALITY_SENSOR

#include "../detect/reClockI2C.h"
#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "PMSA003ISensor.h"
#include "TelemetrySensor.h"

#include <Wire.h>

PMSA003ISensor::PMSA003ISensor() : TelemetrySensor(meshtastic_TelemetrySensorType_PMSA003I, "PMSA003I") {}

bool PMSA003ISensor::initDevice(TwoWire *bus, ScanI2C::FoundDevice *dev)
{
    LOG_INFO("Init sensor: %s", sensorName);
#ifdef PMSA003I_ENABLE_PIN
    pinMode(PMSA003I_ENABLE_PIN, OUTPUT);
#endif

    _bus = bus;
    _address = dev->address.address;

#if defined(PMSA003I_I2C_CLOCK_SPEED) && defined(CAN_RECLOCK_I2C)
    uint32_t currentClock = reClockI2C(PMSA003I_I2C_CLOCK_SPEED, _bus);
    if (!currentClock) {
        LOG_WARN("PMSA003I can't be used at this clock speed");
        return false;
    }
#endif

    _bus->beginTransmission(_address);
    if (_bus->endTransmission() != 0) {
        LOG_WARN("PMSA003I not found on I2C at 0x12");
        return false;
    }

#if defined(PMSA003I_I2C_CLOCK_SPEED) && defined(CAN_RECLOCK_I2C)
    reClockI2C(currentClock, _bus);
#endif

    status = 1;
    LOG_INFO("PMSA003I Enabled");

    initI2CSensor();
    return true;
}

bool PMSA003ISensor::getMetrics(meshtastic_Telemetry *measurement)
{
    if (!isActive()) {
        LOG_WARN("PMSA003I is not active");
        return false;
    }

#if defined(PMSA003I_I2C_CLOCK_SPEED) && defined(CAN_RECLOCK_I2C)
    uint32_t currentClock = reClockI2C(PMSA003I_I2C_CLOCK_SPEED, _bus);
#endif

    _bus->requestFrom(_address, PMSA003I_FRAME_LENGTH);
    if (_bus->available() < PMSA003I_FRAME_LENGTH) {
        LOG_WARN("PMSA003I read failed: incomplete data (%d bytes)", _bus->available());
        return false;
    }

#if defined(PMSA003I_I2C_CLOCK_SPEED) && defined(CAN_RECLOCK_I2C)
    reClockI2C(currentClock, _bus);
#endif

    for (uint8_t i = 0; i < PMSA003I_FRAME_LENGTH; i++) {
        buffer[i] = _bus->read();
    }

    if (buffer[0] != 0x42 || buffer[1] != 0x4D) {
        LOG_WARN("PMSA003I frame header invalid: 0x%02X 0x%02X", buffer[0], buffer[1]);
        return false;
    }

    auto read16 = [](uint8_t *data, uint8_t idx) -> uint16_t { return (data[idx] << 8) | data[idx + 1]; };

    computedChecksum = 0;

    for (uint8_t i = 0; i < PMSA003I_FRAME_LENGTH - 2; i++) {
        computedChecksum += buffer[i];
    }
    receivedChecksum = read16(buffer, PMSA003I_FRAME_LENGTH - 2);

    if (computedChecksum != receivedChecksum) {
        LOG_WARN("PMSA003I checksum failed: computed 0x%04X, received 0x%04X", computedChecksum, receivedChecksum);
        return false;
    }

    measurement->variant.air_quality_metrics.has_pm10_standard = true;
    measurement->variant.air_quality_metrics.pm10_standard = read16(buffer, 4);

    measurement->variant.air_quality_metrics.has_pm25_standard = true;
    measurement->variant.air_quality_metrics.pm25_standard = read16(buffer, 6);

    measurement->variant.air_quality_metrics.has_pm100_standard = true;
    measurement->variant.air_quality_metrics.pm100_standard = read16(buffer, 8);

    // TODO - Add admin command to remove environmental metrics to save protobuf space
    measurement->variant.air_quality_metrics.has_pm10_environmental = true;
    measurement->variant.air_quality_metrics.pm10_environmental = read16(buffer, 10);

    measurement->variant.air_quality_metrics.has_pm25_environmental = true;
    measurement->variant.air_quality_metrics.pm25_environmental = read16(buffer, 12);

    measurement->variant.air_quality_metrics.has_pm100_environmental = true;
    measurement->variant.air_quality_metrics.pm100_environmental = read16(buffer, 14);

    // TODO - Add admin command to remove PN to save protobuf space
    measurement->variant.air_quality_metrics.has_particles_03um = true;
    measurement->variant.air_quality_metrics.particles_03um = read16(buffer, 16);

    measurement->variant.air_quality_metrics.has_particles_05um = true;
    measurement->variant.air_quality_metrics.particles_05um = read16(buffer, 18);

    measurement->variant.air_quality_metrics.has_particles_10um = true;
    measurement->variant.air_quality_metrics.particles_10um = read16(buffer, 20);

    measurement->variant.air_quality_metrics.has_particles_25um = true;
    measurement->variant.air_quality_metrics.particles_25um = read16(buffer, 22);

    measurement->variant.air_quality_metrics.has_particles_50um = true;
    measurement->variant.air_quality_metrics.particles_50um = read16(buffer, 24);

    measurement->variant.air_quality_metrics.has_particles_100um = true;
    measurement->variant.air_quality_metrics.particles_100um = read16(buffer, 26);

    return true;
}

bool PMSA003ISensor::isActive()
{
    return state == State::ACTIVE;
}

void PMSA003ISensor::sleep()
{
#ifdef PMSA003I_ENABLE_PIN
    digitalWrite(PMSA003I_ENABLE_PIN, LOW);
    state = State::IDLE;
#endif
}

uint32_t PMSA003ISensor::wakeUp()
{
#ifdef PMSA003I_ENABLE_PIN
    LOG_INFO("Waking up PMSA003I");
    digitalWrite(PMSA003I_ENABLE_PIN, HIGH);
    state = State::ACTIVE;
    return PMSA003I_WARMUP_MS;
#endif
    // No need to wait for warmup if already active
    return 0;
}
#endif
