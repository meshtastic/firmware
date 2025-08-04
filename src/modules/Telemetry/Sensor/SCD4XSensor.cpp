#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && __has_include(<SensirionI2cScd4x.h>)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "SCD4XSensor.h"
#include "TelemetrySensor.h"
#include <SensirionI2cScd4x.h>

#define SCD4X_NO_ERROR 0

SCD4XSensor::SCD4XSensor() : TelemetrySensor(meshtastic_TelemetrySensorType_SCD4X, "SCD4X") {}

#ifdef SCD4X_I2C_CLOCK_SPEED
uint32_t SCD4XSensor::setI2CClock(uint32_t desiredClock){
    uint32_t currentClock;
    currentClock = bus->getClock();
    LOG_DEBUG("Current I2C clock: %uHz", currentClock);
    if (currentClock != desiredClock){
        LOG_DEBUG("Setting I2C clock to: %uHz", desiredClock);
        bus->setClock(desiredClock);
        return currentClock;
    }
    return 0;
}
#endif

int32_t SCD4XSensor::runOnce()
{
    LOG_INFO("Init sensor: %s", sensorName);
    if (!hasSensor()) {
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    }

    bus = nodeTelemetrySensorsMap[sensorType].second;
    address = (uint8_t)nodeTelemetrySensorsMap[sensorType].first;

#ifdef SCD4X_I2C_CLOCK_SPEED
    uint32_t currentClock;
    currentClock = setI2CClock(SCD4X_I2C_CLOCK_SPEED);
#endif

    // FIXME - This should be based on bus and address from above
    scd4x.begin(*nodeTelemetrySensorsMap[sensorType].second,
        address);

    // SCD4X library
    delay(30);

    // Stop periodic measurement
    if (!stopMeasurement()) {
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    }

    // Get sensor variant
    scd4x.getSensorVariant(sensorVariant);

    if (sensorVariant == SCD4X_SENSOR_VARIANT_SCD41){
        LOG_INFO("SCD4X: Found SCD41");
        if (!wakeUp()) {
            LOG_ERROR("SCD4X: Error trying to execute wakeUp()");
            return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
        }
    }

    if (!getASC(ascActive)){
        LOG_ERROR("SCD4X: Unable to check if ASC is enabled");
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    }

    // Start measurement in selected power mode (low power by default)
    if (!startMeasurement()){
        LOG_ERROR("SCD4X: Couldn't start measurement");
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    }

#ifdef SCD4X_I2C_CLOCK_SPEED
    if (currentClock){
        setI2CClock(currentClock);
    }
#endif

    if (state == SCD4X_MEASUREMENT){
        status = 1;
    } else {
        status = 0;
    }

    return initI2CSensor();
}

void SCD4XSensor::setup() {}

bool SCD4XSensor::getMetrics(meshtastic_Telemetry *measurement)
{

    if (state != SCD4X_MEASUREMENT) {
        LOG_ERROR("SCD4X: Not in measurement mode");
        return false;
    }

    uint16_t co2, error;
    float temperature, humidity;

#ifdef SCD4X_I2C_CLOCK_SPEED
    uint32_t currentClock;
    currentClock = setI2CClock(SCD4X_I2C_CLOCK_SPEED);
#endif

    bool dataReady;
    error = scd4x.getDataReadyStatus(dataReady);
    if (!dataReady) {
        LOG_ERROR("SCD4X: Data is not ready");
        return false;
    }

    error = scd4x.readMeasurement(co2, temperature, humidity);

#ifdef SCD4X_I2C_CLOCK_SPEED
    if (currentClock){
        setI2CClock(currentClock);
    }
#endif

    LOG_DEBUG("SCD4X readings: %u ppm, %.2f degC, %.2f %rh", co2, temperature, humidity);
    if (error != SCD4X_NO_ERROR) {
        LOG_DEBUG("SCD4X: Error while getting measurements: %u", error);
        if (co2 == 0) {
            LOG_ERROR("SCD4X: Skipping invalid measurement.");
        }
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
    uint16_t error, frcCorr;

    LOG_INFO("SCD4X: Issuing FRC. Ensure device has been working at least 3 minutes in stable target environment");

    if (!stopMeasurement()) {
        return false;
    }

    LOG_INFO("SCD4X: Target CO2: %u ppm", targetCO2);
    error = scd4x.performForcedRecalibration((uint16_t)targetCO2, frcCorr);

    // SCD4X Sensirion datasheet
    delay(400);

    if (error != SCD4X_NO_ERROR){
        LOG_ERROR("SCD4X: Unable to perform forced recalibration.");
        return false;
    }

    if (frcCorr == 0xFFFF) {
        LOG_ERROR("SCD4X: Error while performing forced recalibration.");
        return false;
    }

    LOG_INFO("SCD4X: FRC Correction successful. Correction output: %u", (uint16_t)(frcCorr-0x8000));

    return true;
}

bool SCD4XSensor::startMeasurement() {
    uint16_t error;

    if (state == SCD4X_MEASUREMENT){
        LOG_DEBUG("SCD4X: Already in measurement mode");
        return true;
    }

    if (lowPower) {
        error = scd4x.startLowPowerPeriodicMeasurement();
    } else {
        error = scd4x.startPeriodicMeasurement();
    }

    if (error == SCD4X_NO_ERROR) {
        LOG_INFO("SCD4X: Started measurement mode");
        if (lowPower) {
            LOG_INFO("SCD4X: Low power mode");
        } else {
            LOG_INFO("SCD4X: Normal power mode");
        }

        state = SCD4X_MEASUREMENT;
        return true;
    } else {
        LOG_ERROR("SCD4X: Couldn't start measurement mode");
        return false;
    }
}

bool SCD4XSensor::stopMeasurement(){
    uint16_t error;

    error = scd4x.stopPeriodicMeasurement();
    if (error != SCD4X_NO_ERROR) {
        LOG_ERROR("SCD4X: Unable to set idle mode on SCD4X.");
        return false;
    }

    state = SCD4X_IDLE;
    return true;
}

bool SCD4XSensor::setPowerMode(bool _lowPower) {
    lowPower = _lowPower;

    if (!stopMeasurement()) {
        return false;
    }

    if (lowPower) {
        LOG_DEBUG("SCD4X: Set low power mode");
    } else {
        LOG_DEBUG("SCD4X: Set normal power mode");
    }

    return true;
}

/**
* @brief Check the current mode (ASC or FRC)

* From Sensirion SCD4X I2C Library
*/
bool SCD4XSensor::getASC(uint16_t &_ascActive) {
    uint16_t error;
    LOG_INFO("SCD4X: Getting ASC");

    if (!stopMeasurement()) {
        return false;
    }
    error = scd4x.getAutomaticSelfCalibrationEnabled(_ascActive);

    if (error != SCD4X_NO_ERROR){
        LOG_ERROR("SCD4X: Unable to send command.");
        return false;
    }

    if (_ascActive){
        LOG_INFO("SCD4X: ASC is enabled");
    } else {
        LOG_INFO("SCD4X: FRC is enabled");
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

    if (!stopMeasurement()) {
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
    // TODO - Remove?
    //  Available in library, but not described in datasheet.
    uint16_t error;
    LOG_INFO("SCD4X: Setting ASC baseline to: %u", targetCO2);

    getASC(ascActive);
    if (!ascActive){
        LOG_ERROR("SCD4X: Can't set ASC baseline. ASC is not active");
        return false;
    }

    if (!stopMeasurement()) {
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

    LOG_INFO("SCD4X: Setting ASC baseline successful");

    return true;
}


/**
* @brief Set the temperature compensation reference.
*
* From Sensirion SCD4X I2C Library.
*
* Setting the temperature offset of the SCD4x inside the customer device
* allows the user to optimize the RH and T output signal.
* By default, the temperature offset is set to 4 °C. To save
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
    float updatedTempOffset;
    float tempOffset;
    bool dataReady;
    uint16_t co2;
    float temperature;
    float humidity;

    LOG_INFO("SCD4X: Setting reference temperature at: %.2f", tempReference);

    error = scd4x.getDataReadyStatus(dataReady);
    if (!dataReady) {
        LOG_ERROR("SCD4X: Data is not ready");
        return false;
    }

    error = scd4x.readMeasurement(co2, temperature, humidity);
    if (error != SCD4X_NO_ERROR) {
        LOG_ERROR("SCD4X: Unable to read current temperature. Error code: %u", error);
        return false;
    }

    LOG_INFO("SCD4X: Current sensor temperature: %.2f", temperature);

    if (!stopMeasurement()) {
        return false;
    }

    error = scd4x.getTemperatureOffset(prevTempOffset);

    if (error != SCD4X_NO_ERROR){
        LOG_ERROR("SCD4X: Unable to get temperature offset. Error code: %u", error);
        return false;
    }
    LOG_INFO("SCD4X: Current sensor temperature offset: %.2f", prevTempOffset);

    tempOffset = temperature - tempReference + prevTempOffset;

    LOG_INFO("SCD4X: Setting temperature offset: %.2f", tempOffset);
    error = scd4x.setTemperatureOffset(tempOffset);
    if (error != SCD4X_NO_ERROR){
        LOG_ERROR("SCD4X: Unable to set temperature offset. Error code: %u", error);
        return false;
    }

    error = scd4x.persistSettings();
    if (error != SCD4X_NO_ERROR){
        LOG_ERROR("SCD4X: Unable to make settings persistent. Error code: %u", error);
        return false;
    }

    scd4x.getTemperatureOffset(updatedTempOffset);
    LOG_INFO("SCD4X: Updated sensor temperature offset: %.2f", updatedTempOffset);

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

    if (!stopMeasurement()) {
        return false;
    }

    error = scd4x.getSensorAltitude(altitude);

    if (error != SCD4X_NO_ERROR){
        LOG_ERROR("SCD4X: Unable to get altitude. Error code: %u", error);
        return false;
    }
    LOG_INFO("SCD4X: Sensor altitude: %u", altitude);

    return true;
}

/**
* @brief Get the ambient pressure around the sensor.
*
* From Sensirion SCD4X I2C Library.
*
* Gets the ambient pressure in Pa.
*/
bool SCD4XSensor::getAmbientPressure(uint32_t &ambientPressure){
    uint16_t error;
    LOG_INFO("SCD4X: Requesting sensor ambient pressure");

    error = scd4x.getAmbientPressure(ambientPressure);

    if (error != SCD4X_NO_ERROR){
        LOG_ERROR("SCD4X: Unable to get altitude. Error code: %u", error);
        return false;
    }
    LOG_INFO("SCD4X: Sensor ambient pressure: %u", ambientPressure);

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

    if (!stopMeasurement()) {
        return false;
    }

    error = scd4x.setSensorAltitude(altitude);

    if (error != SCD4X_NO_ERROR){
        LOG_ERROR("SCD4X: Unable to set altitude. Error code: %u", error);
        return false;
    }

    error = scd4x.persistSettings();
    if (error != SCD4X_NO_ERROR){
        LOG_ERROR("SCD4X: Unable to make settings persistent. Error code: %u", error);
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
        LOG_ERROR("SCD4X: Unable to set altitude. Error code: %u", error);
        return false;
    }

    // Sensirion doesn't indicate if this is necessary. We send it anyway
    error = scd4x.persistSettings();
    if (error != SCD4X_NO_ERROR){
        LOG_ERROR("SCD4X: Unable to make settings persistent. Error code: %u", error);
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

    LOG_INFO("SCD4X: Requesting factory reset");

    if (!stopMeasurement()) {
        return false;
    }

    error = scd4x.performFactoryReset();

    if (error != SCD4X_NO_ERROR){
        LOG_ERROR("SCD4X: Unable to do factory reset. Error code: %u", error);
        return false;
    }

    LOG_INFO("SCD4X: Factory reset successful");

    return true;
}

/**
* @brief Put the sensor into sleep mode from idle mode.
*
* From Sensirion SCD4X I2C Library.
*
* Put the sensor from idle to sleep to reduce power consumption. Can be
* used to power down when operating the sensor in power-cycled single shot
* mode.
*
* @note This command is only available in idle mode. Only for SCD41.
*/
bool SCD4XSensor::sleep() {
    LOG_INFO("SCD4X: Powering down");

    if (sensorVariant != SCD4X_SENSOR_VARIANT_SCD41) {
        LOG_WARN("SCD4X: Can't send sensor to sleep. Incorrect variant. Ignoring");
        return true;
    }

    if (!stopMeasurement()) {
        return false;
    }

    if (scd4x.powerDown() != SCD4X_NO_ERROR) {
        LOG_ERROR("SCD4X: Error trying to execute wakeUp()");
        return false;
    }
    state = SCD4X_OFF;
    return true;
}

/**
* @brief Wake up sensor from sleep mode to idle mode.
*
* From Sensirion SCD4X I2C Library.
*
* Wake up the sensor from sleep mode into idle mode. Note that the SCD4x
* does not acknowledge the wake_up command. The sensor's idle state after
* wake up can be verified by reading out the serial number.
*
* @note This command is only available for SCD41.
*/
bool SCD4XSensor::wakeUp(){
    LOG_INFO("SCD4X: Waking up");

    if (scd4x.wakeUp() != SCD4X_NO_ERROR) {
        LOG_ERROR("SCD4X: Error trying to execute wakeUp()");
        return false;
    }
    state = SCD4X_IDLE;
    return true;
}

AdminMessageHandleResult SCD4XSensor::handleAdminMessage(const meshtastic_MeshPacket &mp, meshtastic_AdminMessage *request,
                                                           meshtastic_AdminMessage *response)
{
    AdminMessageHandleResult result;

    // TODO: potentially add selftest command?

    switch (request->which_payload_variant) {
        case meshtastic_AdminMessage_sensor_config_tag:
            // Check for ASC-FRC request first
            if (!request->sensor_config.has_scd4x_config) {
                result = AdminMessageHandleResult::NOT_HANDLED;
                break;
            }

            if (request->sensor_config.scd4x_config.has_factory_reset) {
                LOG_DEBUG("SCD4X: Requested factory reset");
                this->factoryReset();
            } else {

                if (request->sensor_config.scd4x_config.has_set_asc) {
                    this->setASC(request->sensor_config.scd4x_config.set_asc);
                    if (request->sensor_config.scd4x_config.set_asc == false) {
                        LOG_DEBUG("SCD4X: Request for FRC");
                        if (request->sensor_config.scd4x_config.has_set_target_co2_conc) {
                            this->performFRC(request->sensor_config.scd4x_config.set_target_co2_conc);
                        } else {
                            // FRC requested but no target CO2 provided
                            LOG_ERROR("SCD4X: target CO2 not provided");
                            result = AdminMessageHandleResult::NOT_HANDLED;
                            break;
                        }
                    } else {
                        LOG_DEBUG("SCD4X: Request for ASC");
                        if (request->sensor_config.scd4x_config.has_set_target_co2_conc) {
                            LOG_DEBUG("SCD4X: Request has target CO2");
                            // TODO - Remove? see setASCBaseline function
                            this->setASCBaseline(request->sensor_config.scd4x_config.set_target_co2_conc);
                        } else {
                            LOG_DEBUG("SCD4X: Request doesn't have target CO2");
                        }
                    }
                }

                // Check for temperature offset
                // NOTE: this requires to have a sensor working on stable environment
                // And to make it between readings
                if (request->sensor_config.scd4x_config.has_set_temperature) {
                    this->setTemperature(request->sensor_config.scd4x_config.set_temperature);
                }

                // Check for altitude or pressure offset
                if (request->sensor_config.scd4x_config.has_set_altitude) {
                    this->setAltitude(request->sensor_config.scd4x_config.set_altitude);
                } else if (request->sensor_config.scd4x_config.has_set_ambient_pressure){
                    this->setAmbientPressure(request->sensor_config.scd4x_config.set_ambient_pressure);
                }

                // Check for low power mode
                // NOTE: to switch from one mode to another do:
                // setPowerMode -> startMeasurement
                if (request->sensor_config.scd4x_config.has_set_power_mode) {
                    this->setPowerMode(request->sensor_config.scd4x_config.set_power_mode);
                }

            }

            // Start measurement mode
            this->startMeasurement();

            result = AdminMessageHandleResult::HANDLED;
            break;

        default:
            result = AdminMessageHandleResult::NOT_HANDLED;
    }

    return result;
}

#endif