#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "PMSA003ISensor.h"
#include "TelemetrySensor.h"

#include <Wire.h>

PMSA003ISensor::PMSA003ISensor()
    : TelemetrySensor(meshtastic_TelemetrySensorType_PMSA003I, "PMSA003I")
{
}

void PMSA003ISensor::setup()
{
#ifdef PMSA003I_ENABLE_PIN
    pinMode(PMSA003I_ENABLE_PIN, OUTPUT);
#endif
}

bool PMSA003ISensor::restoreClock(uint32_t currentClock){
#ifdef PMSA003I_I2C_CLOCK_SPEED
    if (currentClock != PMSA003I_I2C_CLOCK_SPEED){
        // LOG_DEBUG("Restoring I2C clock to %uHz", currentClock);
        return bus->setClock(currentClock);
    }
    return true;
#endif
}

int32_t PMSA003ISensor::runOnce()
{
    LOG_INFO("Init sensor: %s", sensorName);

    if (!hasSensor()) {
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    }

    bus = nodeTelemetrySensorsMap[sensorType].second;
    address = (uint8_t)nodeTelemetrySensorsMap[sensorType].first;

#ifdef PMSA003I_I2C_CLOCK_SPEED
    uint32_t currentClock;
    currentClock = bus->getClock();
    if (currentClock != PMSA003I_I2C_CLOCK_SPEED){
        // LOG_DEBUG("Changing I2C clock to %u", PMSA003I_I2C_CLOCK_SPEED);
        bus->setClock(PMSA003I_I2C_CLOCK_SPEED);
    }
#endif

    bus->beginTransmission(address);
    if (bus->endTransmission() != 0) {
        LOG_WARN("PMSA003I not found on I2C at 0x12");
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    }

    restoreClock(currentClock);

    status = 1; 
    LOG_INFO("PMSA003I Enabled");

    return initI2CSensor();
}

bool PMSA003ISensor::getMetrics(meshtastic_Telemetry *measurement)
{
    if(!isActive()){
        LOG_WARN("PMSA003I is not active");
        return false;
    }

#ifdef PMSA003I_I2C_CLOCK_SPEED
    uint32_t currentClock;
    currentClock = bus->getClock();
    if (currentClock != PMSA003I_I2C_CLOCK_SPEED){
        // LOG_DEBUG("Changing I2C clock to %u", PMSA003I_I2C_CLOCK_SPEED);
        bus->setClock(PMSA003I_I2C_CLOCK_SPEED);
    }
#endif

    bus->requestFrom(address, PMSA003I_FRAME_LENGTH);
    if (bus->available() < PMSA003I_FRAME_LENGTH) {
        LOG_WARN("PMSA003I read failed: incomplete data (%d bytes)", bus->available());
        return false;
    }

    restoreClock(currentClock);

    for (uint8_t i = 0; i < PMSA003I_FRAME_LENGTH; i++) {
        buffer[i] = bus->read();
    }

    if (buffer[0] != 0x42 || buffer[1] != 0x4D) {
        LOG_WARN("PMSA003I frame header invalid: 0x%02X 0x%02X", buffer[0], buffer[1]);
        return false;
    }

    auto read16 = [](uint8_t *data, uint8_t idx) -> uint16_t {
        return (data[idx] << 8) | data[idx + 1];
    };

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

    measurement->variant.air_quality_metrics.has_pm10_environmental = true;
    measurement->variant.air_quality_metrics.pm10_environmental = read16(buffer, 10);

    measurement->variant.air_quality_metrics.has_pm25_environmental = true;
    measurement->variant.air_quality_metrics.pm25_environmental = read16(buffer, 12);

    measurement->variant.air_quality_metrics.has_pm100_environmental = true;
    measurement->variant.air_quality_metrics.pm100_environmental = read16(buffer, 14);

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

#ifdef PMSA003I_ENABLE_PIN
void PMSA003ISensor::sleep()
{
    digitalWrite(PMSA003I_ENABLE_PIN, LOW);
    state = State::IDLE;
}

uint32_t PMSA003ISensor::wakeUp()
{
    digitalWrite(PMSA003I_ENABLE_PIN, HIGH);
    state = State::ACTIVE;
    return PMSA003I_WARMUP_MS;
}
#endif

#endif
