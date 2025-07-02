#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && __has_include(<Adafruit_PM25AQI.h>)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "PMSA003ISensor.h"
#include "TelemetrySensor.h"
#include "detect/ScanI2CTwoWire.h"
#include <Adafruit_PM25AQI.h>

PMSA003ISensor::PMSA003ISensor() : TelemetrySensor(meshtastic_TelemetrySensorType_PMSA003I, "PMSA003I") {}

int32_t PMSA003ISensor::runOnce()
{
    LOG_INFO("Init sensor: %s", sensorName);
    if (!hasSensor()) {
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    }

#ifdef PMSA003I_ENABLE_PIN
// TODO not sure why this was like this
    sleep();
#endif /* PMSA003I_ENABLE_PIN */

    if (!pmsa003i.begin_I2C()){
#ifndef I2C_NO_RESCAN
        LOG_WARN("Could not establish i2c connection to AQI sensor. Rescan");
        // rescan for late arriving sensors. AQI Module starts about 10 seconds into the boot so this is plenty.
        uint8_t i2caddr_scan[] = {PMSA0031_ADDR};
        uint8_t i2caddr_asize = 1;
        auto i2cScanner = std::unique_ptr<ScanI2CTwoWire>(new ScanI2CTwoWire());
#if defined(I2C_SDA1)
        i2cScanner->scanPort(ScanI2C::I2CPort::WIRE1, i2caddr_scan, i2caddr_asize);
#endif
        i2cScanner->scanPort(ScanI2C::I2CPort::WIRE, i2caddr_scan, i2caddr_asize);
        auto found = i2cScanner->find(ScanI2C::DeviceType::PMSA0031);
        if (found.type != ScanI2C::DeviceType::NONE) {
            nodeTelemetrySensorsMap[meshtastic_TelemetrySensorType_PMSA003I].first = found.address.address;
            nodeTelemetrySensorsMap[meshtastic_TelemetrySensorType_PMSA003I].second =
                i2cScanner->fetchI2CBus(found.address);
            return initI2CSensor();
        }
#endif
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    }
    return initI2CSensor();
}

void PMSA003ISensor::setup()
{
#ifdef PMSA003I_ENABLE_PIN
    pinMode(PMSA003I_ENABLE_PIN, OUTPUT);
#endif /* PMSA003I_ENABLE_PIN */
}

#ifdef PMSA003I_ENABLE_PIN
void PMSA003ISensor::sleep() {
    digitalWrite(PMSA003I_ENABLE_PIN, LOW);
    state = State::IDLE;
}

uint32_t PMSA003ISensor::wakeUp() {
    digitalWrite(PMSA003I_ENABLE_PIN, HIGH);
    state = State::ACTIVE;
}
#endif /* PMSA003I_ENABLE_PIN */

bool PMSA003ISensor::isActive() {
    return state == State::ACTIVE;
}

bool PMSA003ISensor::getMetrics(meshtastic_Telemetry *measurement)
{
    if (!pmsa003i.read(&pmsa003iData)) {
        LOG_WARN("Skip send measurements. Could not read AQI");
        return false;
    }

    measurement->variant.air_quality_metrics.has_pm10_standard = true;
    measurement->variant.air_quality_metrics.pm10_standard = pmsa003iData.pm10_standard;
    measurement->variant.air_quality_metrics.has_pm25_standard = true;
    measurement->variant.air_quality_metrics.pm25_standard = pmsa003iData.pm25_standard;
    measurement->variant.air_quality_metrics.has_pm100_standard = true;
    measurement->variant.air_quality_metrics.pm100_standard = pmsa003iData.pm100_standard;

    measurement->variant.air_quality_metrics.has_pm10_environmental = true;
    measurement->variant.air_quality_metrics.pm10_environmental = pmsa003iData.pm10_env;
    measurement->variant.air_quality_metrics.has_pm25_environmental = true;
    measurement->variant.air_quality_metrics.pm25_environmental = pmsa003iData.pm25_env;
    measurement->variant.air_quality_metrics.has_pm100_environmental = true;
    measurement->variant.air_quality_metrics.pm100_environmental = pmsa003iData.pm100_env;

    return true;
}

#endif