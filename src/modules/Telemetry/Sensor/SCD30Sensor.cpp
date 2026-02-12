#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_AIR_QUALITY_SENSOR && __has_include(<SensirionI2cScd30.h>)

#include "../detect/reClockI2C.h"
#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "SCD30Sensor.h"

#define SCD30_NO_ERROR 0

SCD30Sensor::SCD30Sensor() : TelemetrySensor(meshtastic_TelemetrySensorType_SCD30, "SCD30") {}

bool SCD30Sensor::initDevice(TwoWire *bus, ScanI2C::FoundDevice *dev)
{
    LOG_INFO("Init sensor: %s", sensorName);

    _bus = bus;
    _address = dev->address.address;

#ifdef SCD30_I2C_CLOCK_SPEED
#ifdef CAN_RECLOCK_I2C
    uint32_t currentClock = reClockI2C(SCD30_I2C_CLOCK_SPEED, _bus, false);
#elif !HAS_SCREEN
    reClockI2C(SCD30_I2C_CLOCK_SPEED, _bus, true);
#else
    LOG_WARN("%s can't be used at this clock speed, with a screen", sensorName);
    return false;
#endif /* CAN_RECLOCK_I2C */
#endif /* SCD30_I2C_CLOCK_SPEED */

    scd30.begin(*_bus, _address);

    if (!startMeasurement()) {
        LOG_ERROR("%s: Failed to start periodic measurement", sensorName);
#if defined(SCD30_I2C_CLOCK_SPEED) && defined(CAN_RECLOCK_I2C)
        reClockI2C(currentClock, _bus, false);
#endif
        return false;
    }

    if (!getASC(ascActive)) {
        LOG_WARN("%s: Could not determine ASC state", sensorName);
    }

#if defined(SCD30_I2C_CLOCK_SPEED) && defined(CAN_RECLOCK_I2C)
    reClockI2C(currentClock, _bus, false);
#endif

    if (state == SCD30_MEASUREMENT) {
        status = 1;
    } else {
        status = 0;
    }

    initI2CSensor();

    return true;
}

bool SCD30Sensor::getMetrics(meshtastic_Telemetry *measurement)
{
    float co2, temperature, humidity;

#ifdef SCD30_I2C_CLOCK_SPEED
#ifdef CAN_RECLOCK_I2C
    uint32_t currentClock = reClockI2C(SCD30_I2C_CLOCK_SPEED, _bus, false);
#elif !HAS_SCREEN
    reClockI2C(SCD30_I2C_CLOCK_SPEED, _bus, true);
#else
    LOG_WARN("%s can't be used at this clock speed, with a screen", sensorName);
    return false;
#endif /* CAN_RECLOCK_I2C */
#endif /* SCD30_I2C_CLOCK_SPEED */

    if (scd30.readMeasurementData(co2, temperature, humidity) != SCD30_NO_ERROR) {
        LOG_ERROR("SCD30: Failed to read measurement data.");
        return false;
    }

#if defined(SCD30_I2C_CLOCK_SPEED) && defined(CAN_RECLOCK_I2C)
    reClockI2C(currentClock, _bus, false);
#endif

    if (co2 == 0) {
        LOG_ERROR("SCD30: Invalid CO₂ reading.");
        return false;
    }

    measurement->variant.air_quality_metrics.has_co2 = true;
    measurement->variant.air_quality_metrics.has_co2_temperature = true;
    measurement->variant.air_quality_metrics.has_co2_humidity = true;
    measurement->variant.air_quality_metrics.co2 = (uint32_t)co2;
    measurement->variant.air_quality_metrics.co2_temperature = temperature;
    measurement->variant.air_quality_metrics.co2_humidity = humidity;

    LOG_DEBUG("Got %s readings: co2=%u, co2_temp=%.2f, co2_hum=%.2f", sensorName, (uint32_t)co2, temperature, humidity);

    return true;
}

bool SCD30Sensor::setMeasurementInterval(uint16_t measInterval)
{
    uint16_t error;

    LOG_INFO("%s: setting measurement interval at %us", sensorName, measInterval);
    error = scd30.setMeasurementInterval(measInterval);

    if (error != SCD30_NO_ERROR) {
        LOG_ERROR("%s: Unable to set measurement interval. Error code: %u", sensorName, error);
        return false;
    }

    getMeasurementInterval(measurementInterval);
    return true;
}

bool SCD30Sensor::getMeasurementInterval(uint16_t &measInterval)
{
    uint16_t error;

    LOG_INFO("%s: getting measurement interval", sensorName);
    error = scd30.getMeasurementInterval(measInterval);

    if (error != SCD30_NO_ERROR) {
        LOG_ERROR("%s: Unable to set measurement interval. Error code: %u", sensorName, error);
        return false;
    }

    LOG_INFO("%s: getting measurement interval is %us", sensorName, measurementInterval);

    return true;
}

/**
 * @brief Start measurement mode
 * @note This function should not change the clock
 */
bool SCD30Sensor::startMeasurement()
{
    uint16_t error;

    if (state == SCD30_MEASUREMENT) {
        LOG_DEBUG("%s: Already in measurement mode", sensorName);
        return true;
    }

    error = scd30.startPeriodicMeasurement(0);

    if (error == SCD30_NO_ERROR) {
        LOG_INFO("%s: Started measurement mode", sensorName);

        state = SCD30_MEASUREMENT;
        return true;
    } else {
        LOG_ERROR("%s: Couldn't start measurement mode", sensorName);
        return false;
    }
}

/**
 * @brief Stop measurement mode
 * @note This function should not change the clock
 */
bool SCD30Sensor::stopMeasurement()
{
    uint16_t error;

    error = scd30.stopPeriodicMeasurement();
    if (error != SCD30_NO_ERROR) {
        LOG_ERROR("%s: Unable to stop measurement", sensorName);
        return false;
    }

    state = SCD30_IDLE;
    return true;
}

bool SCD30Sensor::performFRC(uint16_t targetCO2)
{
    uint16_t error;

    LOG_INFO("%s: Issuing FRC. Ensure device has been working at least 3 minutes in stable target environment", sensorName);

    LOG_INFO("%s: Target CO2: %u ppm", sensorName, targetCO2);
    error = scd30.forceRecalibration((uint16_t)targetCO2);

    if (error != SCD30_NO_ERROR) {
        LOG_ERROR("%s: Unable to perform forced recalibration.", sensorName);
        return false;
    }

    LOG_INFO("%s: FRC Correction successful. Correction output: %u", sensorName);

    return true;
}

bool SCD30Sensor::setASC(bool ascEnabled)
{
    uint16_t error;

    LOG_INFO("%s: %s ASC", sensorName, ascEnabled ? "Enabling" : "Disabling");

    error = scd30.activateAutoCalibration((uint16_t)ascEnabled);

    if (error != SCD30_NO_ERROR) {
        LOG_ERROR("%s: Unable to send command.", sensorName);
        return false;
    }

    if (!getASC(ascActive)) {
        LOG_ERROR("%s: Unable to check if ASC is enabled", sensorName);
        return false;
    }

    return true;
}

bool SCD30Sensor::getASC(uint16_t &_ascActive)
{
    uint16_t error;
    // LOG_INFO("%s: Getting ASC", sensorName);

    error = scd30.getAutoCalibrationStatus(_ascActive);

    if (error != SCD30_NO_ERROR) {
        LOG_ERROR("%s: Unable to send command.", sensorName);
        return false;
    }

    LOG_INFO("%s: ASC is %s", sensorName, _ascActive ? "enabled" : "disabled");

    return true;
}

/**
 * @brief Set the temperature reference. Unit ℃.
 *
 * The on-board RH/T sensor is influenced by thermal self-heating of SCD30
 * and other electrical components. Design-in alters the thermal properties
 * of SCD30 such that temperature and humidity offsets may occur when
 * operating the sensor in end-customer devices. Compensation of those
 * effects is achievable by writing the temperature offset found in
 * continuous operation of the device into the sensor. Temperature offset
 * value is saved in non-volatile memory. The last set value will be used
 * for temperature offset compensation after repowering.
 *
 * @param[in] tempReference
 * @note this function is certainly confusing and it's not recommended
 */
bool SCD30Sensor::setTemperature(float tempReference)
{
    uint16_t error;
    uint16_t updatedTempOffset;
    float tempOffset;
    uint16_t _tempOffset;
    float co2;
    float temperature;
    float humidity;

    if (tempReference == 100) {
        // Requesting the value of 100 will restore the temperature offset
        LOG_INFO("%s: Setting reference temperature at 0degC", sensorName);
        _tempOffset = 0;
    } else {

        LOG_INFO("%s: Setting reference temperature at: %.2f", sensorName, tempReference);

        error = scd30.readMeasurementData(co2, temperature, humidity);
        if (error != SCD30_NO_ERROR) {
            LOG_ERROR("%s: Unable to read current temperature. Error code: %u", sensorName, error);
            return false;
        }

        LOG_INFO("%s: Current sensor temperature: %.2f", sensorName, temperature);

        tempOffset = (temperature - tempReference);
        if (tempOffset < 0) {
            LOG_ERROR("%s temperature offset is only positive", sensorName);
            return false;
        }

        tempOffset *= 100;
        _tempOffset = static_cast<uint16_t>(tempOffset);
        // _tempOffset *= 100; // Avoid numeric issues with float - uint convertions
    }

    LOG_INFO("%s: Setting temperature offset: %u (*100)", sensorName, _tempOffset);

    error = scd30.setTemperatureOffset(_tempOffset);
    if (error != SCD30_NO_ERROR) {
        LOG_ERROR("%s: Unable to set temperature offset. Error code: %u", sensorName, error);
        return false;
    }

    scd30.getTemperatureOffset(updatedTempOffset);
    LOG_INFO("%s: Updated sensor temperature offset: %u (*100)", sensorName, updatedTempOffset);

    return true;
}

bool SCD30Sensor::setAltitude(uint16_t altitude)
{
    uint16_t error;

    LOG_INFO("%s: setting altitude at %um", sensorName, altitude);

    error = scd30.setAltitudeCompensation(altitude);

    if (error != SCD30_NO_ERROR) {
        LOG_ERROR("%s: Unable to set altitude. Error code: %u", sensorName, error);
        return false;
    }

    uint16_t newAltitude;
    getAltitude(newAltitude);

    return true;
}

bool SCD30Sensor::getAltitude(uint16_t &altitude)
{
    uint16_t error;
    // LOG_INFO("%s: Getting altitude", sensorName);

    error = scd30.getAltitudeCompensation(altitude);

    if (error != SCD30_NO_ERROR) {
        LOG_ERROR("%s: Unable to get altitude. Error code: %u", sensorName, error);
        return false;
    }
    LOG_INFO("%s: Sensor altitude: %u", sensorName, altitude);

    return true;
}

bool SCD30Sensor::softReset()
{
    uint16_t error;

    LOG_INFO("%s: Requesting soft reset", sensorName);

    error = scd30.softReset();

    if (error != SCD30_NO_ERROR) {
        LOG_ERROR("%s: Unable to do soft reset. Error code: %u", sensorName, error);
        return false;
    }

    LOG_INFO("%s: soft reset successful", sensorName);

    return true;
}

/**
 * @brief Check if sensor is in measurement mode
 */
bool SCD30Sensor::isActive()
{
    return state == SCD30_MEASUREMENT;
}

/**
 * @brief Start measurement mode
 * @note Not used in admin comands, getMetrics or init, can change clock.
 */
uint32_t SCD30Sensor::wakeUp()
{

#ifdef SCD30_I2C_CLOCK_SPEED
#ifdef CAN_RECLOCK_I2C
    uint32_t currentClock = reClockI2C(SCD30_I2C_CLOCK_SPEED, _bus, false);
#elif !HAS_SCREEN
    reClockI2C(SCD30_I2C_CLOCK_SPEED, _bus, true);
#else
    LOG_WARN("%s can't be used at this clock speed, with a screen", sensorName);
    return 0;
#endif /* CAN_RECLOCK_I2C */
#endif /* SCD30_I2C_CLOCK_SPEED */

    startMeasurement();

#if defined(SCD30_I2C_CLOCK_SPEED) && defined(CAN_RECLOCK_I2C)
    reClockI2C(currentClock, _bus, false);
#endif

    return 0;
}

/**
 * @brief Stop measurement mode
 * @note Not used in admin comands, getMetrics or init, can change clock.
 */
void SCD30Sensor::sleep()
{
#ifdef SCD30_I2C_CLOCK_SPEED
#ifdef CAN_RECLOCK_I2C
    uint32_t currentClock = reClockI2C(SCD30_I2C_CLOCK_SPEED, _bus, false);
#elif !HAS_SCREEN
    reClockI2C(SCD30_I2C_CLOCK_SPEED, _bus, true);
#else
    LOG_WARN("%s can't be used at this clock speed, with a screen", sensorName);
    return;
#endif /* CAN_RECLOCK_I2C */
#endif /* SCD30_I2C_CLOCK_SPEED */

    stopMeasurement();

#if defined(SCD30_I2C_CLOCK_SPEED) && defined(CAN_RECLOCK_I2C)
    reClockI2C(currentClock, _bus, false);
#endif
}

bool SCD30Sensor::canSleep()
{
    return false;
}

int32_t SCD30Sensor::wakeUpTimeMs()
{
    return 0;
}

int32_t SCD30Sensor::pendingForReadyMs()
{
    return 0;
}

AdminMessageHandleResult SCD30Sensor::handleAdminMessage(const meshtastic_MeshPacket &mp, meshtastic_AdminMessage *request,
                                                         meshtastic_AdminMessage *response)
{
    AdminMessageHandleResult result;

#ifdef SCD30_I2C_CLOCK_SPEED
#ifdef CAN_RECLOCK_I2C
    uint32_t currentClock = reClockI2C(SCD30_I2C_CLOCK_SPEED, _bus, false);
#elif !HAS_SCREEN
    reClockI2C(SCD30_I2C_CLOCK_SPEED, _bus, true);
#else
    LOG_WARN("%s can't be used at this clock speed, with a screen", sensorName);
    return AdminMessageHandleResult::NOT_HANDLED;
#endif /* CAN_RECLOCK_I2C */
#endif /* SCD30_I2C_CLOCK_SPEED */

    switch (request->which_payload_variant) {
    case meshtastic_AdminMessage_sensor_config_tag:
        // Check for ASC-FRC request first
        if (!request->sensor_config.has_scd30_config) {
            result = AdminMessageHandleResult::NOT_HANDLED;
            break;
        }

        if (request->sensor_config.scd30_config.has_soft_reset) {
            LOG_DEBUG("%s: Requested soft reset", sensorName);
            this->softReset();
        } else {

            if (request->sensor_config.scd30_config.has_set_asc) {
                this->setASC(request->sensor_config.scd30_config.set_asc);
                if (request->sensor_config.scd30_config.set_asc == false) {
                    LOG_DEBUG("%s: Request for FRC", sensorName);
                    if (request->sensor_config.scd30_config.has_set_target_co2_conc) {
                        this->performFRC(request->sensor_config.scd30_config.set_target_co2_conc);
                    } else {
                        // FRC requested but no target CO2 provided
                        LOG_ERROR("%s: target CO2 not provided", sensorName);
                        result = AdminMessageHandleResult::NOT_HANDLED;
                        break;
                    }
                }
            }

            // Check for temperature offset
            // NOTE: this requires to have a sensor working on stable environment
            // And to make it between readings
            if (request->sensor_config.scd30_config.has_set_temperature) {
                this->setTemperature(request->sensor_config.scd30_config.set_temperature);
            }

            // Check for altitude
            if (request->sensor_config.scd30_config.has_set_altitude) {
                this->setAltitude(request->sensor_config.scd30_config.set_altitude);
            }

            // Check for set measuremen interval
            if (request->sensor_config.scd30_config.has_set_measurement_interval) {
                this->setMeasurementInterval(request->sensor_config.scd30_config.set_measurement_interval);
            }
        }

        result = AdminMessageHandleResult::HANDLED;
        break;

    default:
        result = AdminMessageHandleResult::NOT_HANDLED;
    }

#if defined(SCD30_I2C_CLOCK_SPEED) && defined(CAN_RECLOCK_I2C)
    reClockI2C(currentClock, _bus, false);
#endif

    return result;
}

#endif
