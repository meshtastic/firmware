#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && __has_include(<SensirionI2cScd4x.h>)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "SCD4XSensor.h"
#include "TelemetrySensor.h"
#include <SensirionI2cScd4x.h>

#define SCD4X_NO_ERROR 0

SCD4XSensor::SCD4XSensor() : TelemetrySensor(meshtastic_TelemetrySensorType_SCD4X, "SCD4X") {}

bool SCD4XSensor::restoreClock(uint32_t currentClock){
#ifdef SCD4X_I2C_CLOCK_SPEED
    if (currentClock != SCD4X_I2C_CLOCK_SPEED){
        // LOG_DEBUG("Restoring I2C clock to %uHz", currentClock);
        return bus->setClock(currentClock);
    }
    return true;
#endif
}

int32_t SCD4XSensor::runOnce()
{
    LOG_INFO("Init sensor: %s", sensorName);
    if (!hasSensor()) {
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    }

    uint16_t error;

    bus = nodeTelemetrySensorsMap[sensorType].second;
    address = (uint8_t)nodeTelemetrySensorsMap[sensorType].first;

#ifdef SCD4X_I2C_CLOCK_SPEED
    uint32_t currentClock;
    currentClock = bus->getClock();
    if (currentClock != SCD4X_I2C_CLOCK_SPEED){
        // LOG_DEBUG("Changing I2C clock to %u", SCD4X_I2C_CLOCK_SPEED);
        bus->setClock(SCD4X_I2C_CLOCK_SPEED);
    }
#endif

    // FIXME - This should be based on bus and address from above
    scd4x.begin(*nodeTelemetrySensorsMap[sensorType].second,
        (uint8_t)nodeTelemetrySensorsMap[sensorType].first);

    delay(30);

    // Ensure sensor is in clean state
    error = scd4x.wakeUp();
    if (error != SCD4X_NO_ERROR) {
        LOG_ERROR("SCD4X: Error trying to execute wakeUp()");
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    }

    // Stop periodic measurement
    error = scd4x.stopPeriodicMeasurement();
    if (error != SCD4X_NO_ERROR) {
        LOG_ERROR("SCD4X: Error trying to stopPeriodicMeasurement()");
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    }
    if (!getASC(ascActive)){
        LOG_ERROR("SCD4X: Unable to check if ASC is enabled");
        return false;
    }

    if (!ascActive){
        LOG_INFO("SCD4X: ASC is not active");
    } else {
        LOG_INFO("SCD4X: ASC is active");
    }

    if (!scd4x.startLowPowerPeriodicMeasurement()) {
        status = 1;
    } else {
        status = 0;
    }

    restoreClock(currentClock);

    return initI2CSensor();
}

void SCD4XSensor::setup() {}

bool SCD4XSensor::getMetrics(meshtastic_Telemetry *measurement)
{
    uint16_t co2, error;
    float temperature;
    float humidity;

#ifdef SCD4X_I2C_CLOCK_SPEED
    uint32_t currentClock;
    currentClock = bus->getClock();
    if (currentClock != SCD4X_I2C_CLOCK_SPEED){
        // LOG_DEBUG("Changing I2C clock to %u", SCD4X_I2C_CLOCK_SPEED);
        bus->setClock(SCD4X_I2C_CLOCK_SPEED);
    }
#endif

    error = scd4x.readMeasurement(co2, temperature, humidity);

    restoreClock(currentClock);
    LOG_DEBUG("SCD4X: Error while getting measurements: %u", error);
    LOG_DEBUG("SCD4X readings: %u, %.2f, %.2f", co2, temperature, humidity);
    if (error != SCD4X_NO_ERROR || co2 == 0) {
        LOG_ERROR("SCD4X: Skipping invalid measurement.");
        return false;
    } else {
        measurement->variant.air_quality_metrics.has_co2_temperature = true;
        measurement->variant.air_quality_metrics.has_co2_humidity = true;
        measurement->variant.air_quality_metrics.has_co2 = true;
        measurement->variant.air_quality_metrics.co2_temperature = temperature;
        measurement->variant.air_quality_metrics.co2_humidity = humidity;
        measurement->variant.air_quality_metrics.co2 = co2;
        return true;
    }
}

/**
* @brief Perform a forced recalibration (FRC) of the CO₂ concentration.
*
* From Sensirion SCD4X I2C Library
*
* 1. Operate the SCD4x in the operation mode later used for normal sensor
* operation (e.g. periodic measurement) for at least 3 minutes in an
* environment with a homogenous and constant CO2 concentration. The sensor
* must be operated at the voltage desired for the application when
* performing the FRC sequence. 2. Issue the stop_periodic_measurement
* command. 3. Issue the perform_forced_recalibration command.
*/
bool SCD4XSensor::performFRC(uint32_t targetCO2) {
    uint16_t error;
    uint16_t frcCorr;

    LOG_INFO("SCD4X: Issuing FRC. Ensure device has been working at least 3 minutes in stable target environment");

    error = scd4x.stopPeriodicMeasurement();
    if (error != SCD4X_NO_ERROR) {
        LOG_ERROR("SCD4X: Unable to set idle mode on SCD4X.");
        return false;
    }

    error = scd4x.performForcedRecalibration((uint16_t)targetCO2, frcCorr);

    if (error != SCD4X_NO_ERROR){
        LOG_ERROR("SCD4X: Unable to perform forced recalibration.");
        return false;
    }

    if (frcCorr == 0xFFFF) {
        LOG_ERROR("SCD4X: Error while performing forced recalibration.");
        return false;
    }

    return true;
}

/**
* @brief Check the current mode (ASC or FRC)

* From Sensirion SCD4X I2C Library
*/
bool SCD4XSensor::getASC(uint16_t &ascEnabled) {
    uint16_t error;
    LOG_INFO("SCD4X: Getting ASC");

    error = scd4x.stopPeriodicMeasurement();
    if (error != SCD4X_NO_ERROR) {
        LOG_ERROR("SCD4X: Unable to set idle mode on SCD4X.");
        return false;
    }

    error = scd4x.getAutomaticSelfCalibrationEnabled(ascEnabled);

    if (error != SCD4X_NO_ERROR){
        LOG_ERROR("SCD4X: Unable to send command.");
        return false;
    }
    return true;
}

/**
* @brief Enable or disable automatic self calibration (ASC).
*
* From Sensirion SCD4X I2C Library
*
* Sets the current state (enabled / disabled) of the ASC. By default, ASC
* is enabled.
*/
bool SCD4XSensor::setASC(bool ascEnabled){
    uint16_t error;

    if (ascEnabled){
        LOG_INFO("SCD4X: Enabling ASC");
    } else {
        LOG_INFO("SCD4X: Disabling ASC");
    }

    error = scd4x.stopPeriodicMeasurement();
    if (error != SCD4X_NO_ERROR) {
        LOG_ERROR("SCD4X: Unable to set idle mode on SCD4X.");
        return false;
    }

    error = scd4x.setAutomaticSelfCalibrationEnabled((uint16_t)ascEnabled);

    if (error != SCD4X_NO_ERROR){
        LOG_ERROR("SCD4X: Unable to send command.");
        return false;
    }

    error = scd4x.persistSettings();
    if (error != SCD4X_NO_ERROR){
        LOG_ERROR("SCD4X: Unable to make settings persistent.");
        return false;
    }

    if (!getASC(ascActive)){
        LOG_ERROR("SCD4X: Unable to check if ASC is enabled");
        return false;
    }

    if (ascActive){
        LOG_INFO("SCD4X: ASC is enabled");
    } else {
        LOG_INFO("SCD4X: ASC is disabled");
    }

    return true;
}

/**
* @brief Set the value of ASC baseline target in ppm.
*
* From Sensirion SCD4X I2C Library.
*
* Sets the value of the ASC baseline target, i.e. the CO₂ concentration in
* ppm which the ASC algorithm will assume as lower-bound background to
* which the SCD4x is exposed to regularly within one ASC period of
* operation. To save the setting to the EEPROM, the persist_settings
* command must be issued subsequently. The factory default value is 400
* ppm.
*/
bool SCD4XSensor::setASCBaseline(uint32_t targetCO2){
    uint16_t error;
    LOG_INFO("SCD4X: Setting ASC baseline");

    getASC(ascActive);
    if (!ascActive){
        LOG_ERROR("SCD4X: ASC is not active");
        return false;
    }

    error = scd4x.stopPeriodicMeasurement();
    if (error != SCD4X_NO_ERROR) {
        LOG_ERROR("SCD4X: Unable to set idle mode on SCD4X.");
        return false;
    }

    error = scd4x.setAutomaticSelfCalibrationTarget((uint16_t)targetCO2);

    if (error != SCD4X_NO_ERROR){
        LOG_ERROR("SCD4X: Unable to send command.");
        return false;
    }

    error = scd4x.persistSettings();
    if (error != SCD4X_NO_ERROR){
        LOG_ERROR("SCD4X: Unable to make settings persistent.");
        return false;
    }

    return true;
}


/**
* @brief Set the temperature compensation reference.
*
* From Sensirion SCD4X I2C Library.
*
* Setting the temperature offset of the SCD4x inside the customer device
* allows the user to optimize the RH and T output signal. By default, the temperature offset is set to 4 °C. To save
* the setting to the EEPROM, the persist_settings command may be issued.
* Equation (1) details how the characteristic temperature offset can be
* calculated using the current temperature output of the sensor (TSCD4x), a
* reference temperature value (TReference), and the previous temperature
* offset (Toffset_pervious) obtained using the get_temperature_offset_raw
* command:
*
* Toffset_actual = TSCD4x - TReference + Toffset_pervious.
*
* Recommended temperature offset values are between 0 °C and 20 °C. The
* temperature offset does not impact the accuracy of the CO2 output.
*/
bool SCD4XSensor::setTemperature(float tempReference){
    uint16_t error;
    float prevTempOffset;
    float tempOffset;

    uint16_t co2;
    float temperature;
    float humidity;
    LOG_INFO("SCD4X: Setting reference temperature at: %.2f". temperature);

    error = scd4x.readMeasurement(co2, temperature, humidity);
    if (error != SCD4X_NO_ERROR) {
        LOG_ERROR("SCD4X: Unable read current temperature.");
        return false;
    }

    LOG_INFO("SCD4X: Current sensor temperature: %.2f", temperature);

    error = scd4x.stopPeriodicMeasurement();
    if (error != SCD4X_NO_ERROR) {
        LOG_ERROR("SCD4X: Unable to set idle mode on SCD4X.");
        return false;
    }

    error = scd4x.getTemperatureOffset(prevTempOffset);

    if (error != SCD4X_NO_ERROR){
        LOG_ERROR("SCD4X: Unable to get temperature offset.");
        return false;
    }
    LOG_INFO("SCD4X: Sensor temperature offset: %.2f", prevTempOffset);

    tempOffset = temperature - tempReference + prevTempOffset;

    LOG_INFO("SCD4X: Setting temperature offset: %.2f", tempOffset);
    error = scd4x.setTemperatureOffset(tempOffset);
    if (error != SCD4X_NO_ERROR){
        LOG_ERROR("SCD4X: Unable to set temperature offset.");
        return false;
    }

    error = scd4x.persistSettings();
    if (error != SCD4X_NO_ERROR){
        LOG_ERROR("SCD4X: Unable to make settings persistent.");
        return false;
    }

    return true;
}

/**
* @brief Get the sensor altitude.
*
* From Sensirion SCD4X I2C Library.
*
* Altitude in meters above sea level can be set after device installation.
* Valid value between 0 and 3000m. This overrides pressure offset.
*/
bool SCD4XSensor::getAltitude(uint16_t &altitude){
    uint16_t error;
    LOG_INFO("SCD4X: Requesting sensor altitude");

    error = scd4x.stopPeriodicMeasurement();
    if (error != SCD4X_NO_ERROR) {
        LOG_ERROR("SCD4X: Unable to set idle mode on SCD4X.");
        return false;
    }

    error = scd4x.getSensorAltitude(altitude);

    if (error != SCD4X_NO_ERROR){
        LOG_ERROR("SCD4X: Unable to get altitude.");
        return false;
    }
    LOG_INFO("SCD4X: Sensor altitude: %u", altitude);

    return true;
}

/**
* @brief Set the sensor altitude.
*
* From Sensirion SCD4X I2C Library.
*
* Altitude in meters above sea level can be set after device installation.
* Valid value between 0 and 3000m. This overrides pressure offset.
*/
bool SCD4XSensor::setAltitude(uint32_t altitude){
    uint16_t error;

    error = scd4x.stopPeriodicMeasurement();
    if (error != SCD4X_NO_ERROR) {
        LOG_ERROR("SCD4X: Unable to set idle mode on SCD4X.");
        return false;
    }

    error = scd4x.setSensorAltitude(altitude);

    if (error != SCD4X_NO_ERROR){
        LOG_ERROR("SCD4X: Unable to set altitude.");
        return false;
    }

    error = scd4x.persistSettings();
    if (error != SCD4X_NO_ERROR){
        LOG_ERROR("SCD4X: Unable to make settings persistent.");
        return false;
    }

    return true;
}

/**
* @brief Set the ambient pressure around the sensor.
*
* From Sensirion SCD4X I2C Library.
*
* The set_ambient_pressure command can be sent during periodic measurements
* to enable continuous pressure compensation. Note that setting an ambient
* pressure overrides any pressure compensation based on a previously set
* sensor altitude. Use of this command is highly recommended for
* applications experiencing significant ambient pressure changes to ensure
* sensor accuracy. Valid input values are between 70000 - 120000 Pa. The
* default value is 101300 Pa.
*/
bool SCD4XSensor::setAmbientPressure(uint32_t ambientPressure) {
    uint16_t error;

    error = scd4x.setAmbientPressure(ambientPressure);

    if (error != SCD4X_NO_ERROR){
        LOG_ERROR("SCD4X: Unable to set altitude.");
        return false;
    }

    // Sensirion doesn't indicate if this is necessary. We send it anyway
    error = scd4x.persistSettings();
    if (error != SCD4X_NO_ERROR){
        LOG_ERROR("SCD4X: Unable to make settings persistent.");
        return false;
    }

    return true;
}

/**
* @brief Perform factory reset to erase the settings stored in the EEPROM.
*
* From Sensirion SCD4X I2C Library.
*
* The perform_factory_reset command resets all configuration settings
* stored in the EEPROM and erases the FRC and ASC algorithm history.
*/

bool SCD4XSensor::factoryReset() {
    uint16_t error;

    error = scd4x.stopPeriodicMeasurement();
    if (error != SCD4X_NO_ERROR) {
        LOG_ERROR("SCD4X: Unable to set idle mode on SCD4X.");
        return false;
    }

    error = scd4x.performFactoryReset();

    if (error != SCD4X_NO_ERROR){
        LOG_ERROR("SCD4X: Unable to do factory reset.");
        return false;
    }

    return true;
}



AdminMessageHandleResult SCD4XSensor::handleAdminMessage(const meshtastic_MeshPacket &mp, meshtastic_AdminMessage *request,
                                                           meshtastic_AdminMessage *response)
{
    AdminMessageHandleResult result;

    switch (request->which_payload_variant) {
        case meshtastic_AdminMessage_sensor_config_tag:
            // Check for ASC-FRC request first
            if (!request->sensor_config.has_scdxx_config) {
                result = AdminMessageHandleResult::NOT_HANDLED;
                break;
            }

            if (request->sensor_config.scdxx_config.has_factory_reset) {
                LOG_DEBUG("SCD4X: Requested factory reset");
                this->factoryReset();
            } else {

                if (request->sensor_config.scdxx_config.has_set_asc) {
                    this->setASC(request->sensor_config.scdxx_config.set_asc);
                    if (request->sensor_config.scdxx_config.set_asc == false) {
                        LOG_DEBUG("SCD4X: Request for FRC");
                        if (request->sensor_config.scdxx_config.has_target_co2_conc) {
                            this->performFRC(request->sensor_config.scdxx_config.target_co2_conc);
                        } else {
                            // FRC requested but no target CO2 provided
                            LOG_ERROR("SCD4X: target CO2 not provided");
                            result = AdminMessageHandleResult::NOT_HANDLED;
                            break;
                        }
                    } else {
                        LOG_DEBUG("SCD4X: Request for ASC");
                        if (request->sensor_config.scdxx_config.has_target_co2_conc) {
                            LOG_DEBUG("SCD4X: Request has target CO2");
                            this->setASCBaseline(request->sensor_config.scdxx_config.target_co2_conc);
                        } else {
                            LOG_DEBUG("SCD4X: Request doesn't have target CO2");
                        }
                    }
                }

                // Check for temperature offset
                if (request->sensor_config.scdxx_config.has_temperature) {
                    this->setTemperature(request->sensor_config.scdxx_config.temperature);
                }

                // Check for altitude or pressure offset
                if (request->sensor_config.scdxx_config.has_altitude) {
                    this->setAltitude(request->sensor_config.scdxx_config.altitude);
                } else if (request->sensor_config.scdxx_config.has_ambient_pressure){
                    this->setAmbientPressure(request->sensor_config.scdxx_config.ambient_pressure);
                }

            }

            result = AdminMessageHandleResult::HANDLED;
            break;

        default:
            result = AdminMessageHandleResult::NOT_HANDLED;
    }

    return result;
}

#endif