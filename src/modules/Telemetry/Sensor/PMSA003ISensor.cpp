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

#ifdef PMSA003I_I2C_CLOCK_SPEED
#ifdef CAN_RECLOCK_I2C
    uint32_t currentClock = reClockI2C(PMSA003I_I2C_CLOCK_SPEED, _bus, false);
#elif !HAS_SCREEN
    reClockI2C(PMSA003I_I2C_CLOCK_SPEED, _bus, true);
#else
    LOG_WARN("%s can't be used at this clock speed, with a screen", sensorName);
    return false;
#endif /* CAN_RECLOCK_I2C */
#endif /* PMSA003I_I2C_CLOCK_SPEED */

    _bus->beginTransmission(_address);
    if (_bus->endTransmission() != 0) {
        LOG_WARN("%s not found on I2C at 0x12", sensorName);
        return false;
    }

#if defined(PMSA003I_I2C_CLOCK_SPEED) && defined(CAN_RECLOCK_I2C)
    reClockI2C(currentClock, _bus, false);
#endif

    status = 1;
    LOG_INFO("%s Enabled", sensorName);

    initI2CSensor();
    return true;
}

bool PMSA003ISensor::getMetrics(meshtastic_Telemetry *measurement)
{
    if (!isActive()) {
        LOG_WARN("Can't get metrics. %s is not active", sensorName);
        return false;
    }

#ifdef PMSA003I_I2C_CLOCK_SPEED
#ifdef CAN_RECLOCK_I2C
    uint32_t currentClock = reClockI2C(PMSA003I_I2C_CLOCK_SPEED, _bus, false);
#elif !HAS_SCREEN
    reClockI2C(PMSA003I_I2C_CLOCK_SPEED, _bus, true);
#else
    LOG_WARN("%s can't be used at this clock speed, with a screen", sensorName);
    return false;
#endif /* CAN_RECLOCK_I2C */
#endif /* PMSA003I_I2C_CLOCK_SPEED */

    _bus->requestFrom(_address, (uint8_t)PMSA003I_FRAME_LENGTH);
    if (_bus->available() < PMSA003I_FRAME_LENGTH) {
        LOG_WARN("%s read failed: incomplete data (%d bytes)", sensorName, _bus->available());
        return false;
    }

    for (uint8_t i = 0; i < PMSA003I_FRAME_LENGTH; i++) {
        buffer[i] = _bus->read();
    }

#if defined(PMSA003I_I2C_CLOCK_SPEED) && defined(CAN_RECLOCK_I2C)
    reClockI2C(currentClock, _bus, false);
#endif

    if (buffer[0] != 0x42 || buffer[1] != 0x4D) {
        LOG_WARN("%s frame header invalid: 0x%02X 0x%02X", sensorName, buffer[0], buffer[1]);
        return false;
    }

    auto read16 = [](const uint8_t *data, uint8_t idx) -> uint16_t { return (data[idx] << 8) | data[idx + 1]; };

    computedChecksum = 0;

    for (uint8_t i = 0; i < PMSA003I_FRAME_LENGTH - 2; i++) {
        computedChecksum += buffer[i];
    }
    receivedChecksum = read16(buffer, PMSA003I_FRAME_LENGTH - 2);

    if (computedChecksum != receivedChecksum) {
        LOG_WARN("%s checksum failed: computed 0x%04X, received 0x%04X", sensorName, computedChecksum, receivedChecksum);
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

    LOG_DEBUG("Got %s readings: pM1p0_standard=%u, pM2p5_standard=%u, pM10p0_standard=%u", sensorName,
              measurement->variant.air_quality_metrics.pm10_standard, measurement->variant.air_quality_metrics.pm25_standard,
              measurement->variant.air_quality_metrics.pm100_standard);

    return true;
}

bool PMSA003ISensor::isActive()
{
    return state == State::ACTIVE;
}

int32_t PMSA003ISensor::wakeUpTimeMs()
{
#ifdef PMSA003I_ENABLE_PIN
    return PMSA003I_WARMUP_MS;
#endif
    return 0;
}

int32_t PMSA003ISensor::pendingForReadyMs()
{
#ifdef PMSA003I_ENABLE_PIN

    uint32_t now;
    now = getTime();
    uint32_t sincePmMeasureStarted = (now - pmMeasureStarted) * 1000;
    LOG_DEBUG("%s: Since measure started: %ums", sensorName, sincePmMeasureStarted);

    if (sincePmMeasureStarted < PMSA003I_WARMUP_MS) {
        LOG_INFO("%s: not enough time passed since starting measurement", sensorName);
        return PMSA003I_WARMUP_MS - sincePmMeasureStarted;
    }
    return 0;

#endif
    return 0;
}

bool PMSA003ISensor::canSleep()
{
#ifdef PMSA003I_ENABLE_PIN
    return true;
#endif
    return false;
}

void PMSA003ISensor::sleep()
{
#ifdef PMSA003I_ENABLE_PIN
    digitalWrite(PMSA003I_ENABLE_PIN, LOW);
    state = State::IDLE;
    pmMeasureStarted = 0;
#endif
}

uint32_t PMSA003ISensor::wakeUp()
{
#ifdef PMSA003I_ENABLE_PIN
    LOG_INFO("Waking up %s", sensorName);
    digitalWrite(PMSA003I_ENABLE_PIN, HIGH);
    state = State::ACTIVE;
    pmMeasureStarted = getTime();

    return PMSA003I_WARMUP_MS;
#endif
    // No need to wait for warmup if already active
    return 0;
}
#endif
