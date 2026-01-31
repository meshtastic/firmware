#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_AIR_QUALITY_SENSOR && __has_include(<SensirionI2cScd4x.h>)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "SCD4XSensor.h"
#include "../detect/reClockI2C.h"

#define SCD4X_NO_ERROR 0

SCD4XSensor::SCD4XSensor()
    : TelemetrySensor(meshtastic_TelemetrySensorType_SCD4X, "SCD4X")
{
}

bool SCD4XSensor::initDevice(TwoWire *bus, ScanI2C::FoundDevice *dev)
{
    LOG_INFO("Init sensor: %s", sensorName);

    _bus = bus;
    _address = dev->address.address;

#ifdef SCD4X_I2C_CLOCK_SPEED
#ifdef CAN_RECLOCK_I2C
    uint32_t currentClock = reClockI2C(SCD4X_I2C_CLOCK_SPEED, _bus, false);
    if (currentClock != SCD4X_I2C_CLOCK_SPEED){
        LOG_WARN("%s can't be used at this clock speed (%u)", sensorName, currentClock);
        return false;
    }
#elif !HAS_SCREEN
    reClockI2C(SCD4X_I2C_CLOCK_SPEED, _bus, true);
#else
    LOG_WARN("%s can't be used at this clock speed, with a screen", sensorName);
    return false;
#endif /* CAN_RECLOCK_I2C */
#endif /* SCD4X_I2C_CLOCK_SPEED */

    scd4x.begin(*_bus, _address);

    // From SCD4X library
    delay(30);

    // Stop periodic measurement
    if (!stopMeasurement()) {
        return false;
    }

    // Get sensor variant
    scd4x.getSensorVariant(sensorVariant);

    if (sensorVariant == SCD4X_SENSOR_VARIANT_SCD41){
        LOG_INFO("%s: Found SCD41", sensorName);
        if (!powerUp()) {
            LOG_ERROR("%s: Error trying to execute powerUp()", sensorName);
            return false;
        }
    }

    if (!getASC(ascActive)){
        LOG_ERROR("%s: Unable to check if ASC is enabled", sensorName);
        return false;
    }

    // Start measurement in selected power mode (low power by default)
    if (!startMeasurement()){
        LOG_ERROR("%s: Couldn't start measurement", sensorName);
        return false;
    }

#if defined(SCD4X_I2C_CLOCK_SPEED) && defined(CAN_RECLOCK_I2C)
    reClockI2C(currentClock, _bus, false);
#endif

    if (state == SCD4X_MEASUREMENT){
        status = 1;
    } else {
        status = 0;
    }

    initI2CSensor();

    return true;
}

bool SCD4XSensor::getMetrics(meshtastic_Telemetry *measurement)
{

    if (state != SCD4X_MEASUREMENT) {
        LOG_ERROR("%s: Not in measurement mode", sensorName);
        return false;
    }

    uint16_t co2, error;
    float temperature, humidity;

#ifdef SCD4X_I2C_CLOCK_SPEED
#ifdef CAN_RECLOCK_I2C
    uint32_t currentClock = reClockI2C(SCD4X_I2C_CLOCK_SPEED, _bus, false);
    if (currentClock != SCD4X_I2C_CLOCK_SPEED){
        LOG_WARN("%s can't be used at this clock speed (%u)", sensorName, currentClock);
        return false;
    }
#elif !HAS_SCREEN
    reClockI2C(SCD4X_I2C_CLOCK_SPEED, _bus, true);
#else
    LOG_WARN("%s can't be used at this clock speed, with a screen", sensorName);
    return false;
#endif /* CAN_RECLOCK_I2C */
#endif /* SCD4X_I2C_CLOCK_SPEED */

    bool dataReady;
    error = scd4x.getDataReadyStatus(dataReady);
    if (!dataReady) {
        LOG_ERROR("SCD4X: Data is not ready");
        return false;
    }

    error = scd4x.readMeasurement(co2, temperature, humidity);

#if defined(SCD4X_I2C_CLOCK_SPEED) && defined(CAN_RECLOCK_I2C)
    reClockI2C(currentClock, _bus, false);
#endif

    LOG_DEBUG("%s readings: %u ppm, %.2f degC, %.2f %rh", sensorName, co2, temperature, humidity);
    if (error != SCD4X_NO_ERROR) {
        LOG_DEBUG("%s: Error while getting measurements: %u", sensorName, error);
        if (co2 == 0) {
            LOG_ERROR("%s: Skipping invalid measurement.", sensorName);
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

// TODO
// Make all functions change I2C clock

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

    LOG_INFO("%s: Issuing FRC. Ensure device has been working at least 3 minutes in stable target environment", sensorName);

    if (!stopMeasurement()) {
        return false;
    }

    LOG_INFO("%s: Target CO2: %u ppm", sensorName, targetCO2);
    error = scd4x.performForcedRecalibration((uint16_t)targetCO2, frcCorr);

    // SCD4X Sensirion datasheet
    delay(400);

    if (error != SCD4X_NO_ERROR){
        LOG_ERROR("%s: Unable to perform forced recalibration.", sensorName);
        return false;
    }

    if (frcCorr == 0xFFFF) {
        LOG_ERROR("%s: Error while performing forced recalibration.", sensorName);
        return false;
    }

    LOG_INFO("%s: FRC Correction successful. Correction output: %u", sensorName, (uint16_t)(frcCorr-0x8000));

    return true;
}

bool SCD4XSensor::startMeasurement() {
    uint16_t error;

    if (state == SCD4X_MEASUREMENT){
        LOG_DEBUG("%s: Already in measurement mode", sensorName);
        return true;
    }

    if (lowPower) {
        error = scd4x.startLowPowerPeriodicMeasurement();
    } else {
        error = scd4x.startPeriodicMeasurement();
    }

    if (error == SCD4X_NO_ERROR) {
        LOG_INFO("%s: Started measurement mode", sensorName);
        if (lowPower) {
            LOG_INFO("%s: Low power mode", sensorName);
        } else {
            LOG_INFO("%s: Normal power mode", sensorName);
        }

        state = SCD4X_MEASUREMENT;
        return true;
    } else {
        LOG_ERROR("%s: Couldn't start measurement mode", sensorName);
        return false;
    }
}

bool SCD4XSensor::stopMeasurement(){
    uint16_t error;

    error = scd4x.stopPeriodicMeasurement();
    if (error != SCD4X_NO_ERROR) {
        LOG_ERROR("%s: Unable to set idle mode on SCD4X.", sensorName);
        return false;
    }

    state = SCD4X_IDLE;
    co2MeasureStarted = 0;
    return true;
}

bool SCD4XSensor::setPowerMode(bool _lowPower) {
    lowPower = _lowPower;

    if (!stopMeasurement()) {
        return false;
    }

    if (lowPower) {
        LOG_DEBUG("%s: Set low power mode", sensorName);
    } else {
        LOG_DEBUG("%s: Set normal power mode", sensorName);
    }

    return true;
}

/**
* @brief Check the current mode (ASC or FRC)

* From Sensirion SCD4X I2C Library
*/
bool SCD4XSensor::getASC(uint16_t &_ascActive) {
    uint16_t error;
    LOG_INFO("%s: Getting ASC", sensorName);

    if (!stopMeasurement()) {
        return false;
    }
    error = scd4x.getAutomaticSelfCalibrationEnabled(_ascActive);

    if (error != SCD4X_NO_ERROR){
        LOG_ERROR("%s: Unable to send command.", sensorName);
        return false;
    }

    if (_ascActive){
        LOG_INFO("%s: ASC is enabled", sensorName);
    } else {
        LOG_INFO("%s: FRC is enabled", sensorName);
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
        LOG_INFO("%s: Enabling ASC", sensorName);
    } else {
        LOG_INFO("%s: Disabling ASC", sensorName);
    }

    if (!stopMeasurement()) {
        return false;
    }

    error = scd4x.setAutomaticSelfCalibrationEnabled((uint16_t)ascEnabled);

    if (error != SCD4X_NO_ERROR){
        LOG_ERROR("%s: Unable to send command.", sensorName);
        return false;
    }

    error = scd4x.persistSettings();
    if (error != SCD4X_NO_ERROR){
        LOG_ERROR("%s: Unable to make settings persistent.", sensorName);
        return false;
    }

    if (!getASC(ascActive)){
        LOG_ERROR("%s: Unable to check if ASC is enabled", sensorName);
        return false;
    }

    if (ascActive){
        LOG_INFO("%s: ASC is enabled", sensorName);
    } else {
        LOG_INFO("%s: ASC is disabled", sensorName);
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
    LOG_INFO("%s: Setting ASC baseline to: %u", sensorName, targetCO2);

    getASC(ascActive);
    if (!ascActive){
        LOG_ERROR("%s: Can't set ASC baseline. ASC is not active", sensorName);
        return false;
    }

    if (!stopMeasurement()) {
        return false;
    }

    error = scd4x.setAutomaticSelfCalibrationTarget((uint16_t)targetCO2);

    if (error != SCD4X_NO_ERROR){
        LOG_ERROR("%s: Unable to send command.", sensorName);
        return false;
    }

    error = scd4x.persistSettings();
    if (error != SCD4X_NO_ERROR){
        LOG_ERROR("%s: Unable to make settings persistent.", sensorName);
        return false;
    }

    LOG_INFO("%s: Setting ASC baseline successful", sensorName);

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

    LOG_INFO("%s: Setting reference temperature at: %.2f", sensorName, tempReference);

    error = scd4x.getDataReadyStatus(dataReady);
    if (!dataReady) {
        LOG_ERROR("%s: Data is not ready", sensorName);
        return false;
    }

    error = scd4x.readMeasurement(co2, temperature, humidity);
    if (error != SCD4X_NO_ERROR) {
        LOG_ERROR("%s: Unable to read current temperature. Error code: %u", sensorName, error);
        return false;
    }

    LOG_INFO("%s: Current sensor temperature: %.2f", sensorName, temperature);

    if (!stopMeasurement()) {
        return false;
    }

    error = scd4x.getTemperatureOffset(prevTempOffset);

    if (error != SCD4X_NO_ERROR){
        LOG_ERROR("%s: Unable to get temperature offset. Error code: %u", sensorName, error);
        return false;
    }
    LOG_INFO("%s: Current sensor temperature offset: %.2f", sensorName, prevTempOffset);

    tempOffset = temperature - tempReference + prevTempOffset;

    LOG_INFO("%s: Setting temperature offset: %.2f", sensorName, tempOffset);
    error = scd4x.setTemperatureOffset(tempOffset);
    if (error != SCD4X_NO_ERROR){
        LOG_ERROR("%s: Unable to set temperature offset. Error code: %u", sensorName, error);
        return false;
    }

    error = scd4x.persistSettings();
    if (error != SCD4X_NO_ERROR){
        LOG_ERROR("%s: Unable to make settings persistent. Error code: %u", sensorName, error);
        return false;
    }

    scd4x.getTemperatureOffset(updatedTempOffset);
    LOG_INFO("%s: Updated sensor temperature offset: %.2f", sensorName, updatedTempOffset);

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
    LOG_INFO("%s: Requesting sensor altitude", sensorName);

    if (!stopMeasurement()) {
        return false;
    }

    error = scd4x.getSensorAltitude(altitude);

    if (error != SCD4X_NO_ERROR){
        LOG_ERROR("%s: Unable to get altitude. Error code: %u", sensorName, error);
        return false;
    }
    LOG_INFO("%s: Sensor altitude: %u", sensorName, altitude);

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
    LOG_INFO("%s: Requesting sensor ambient pressure", sensorName);

    error = scd4x.getAmbientPressure(ambientPressure);

    if (error != SCD4X_NO_ERROR){
        LOG_ERROR("%s: Unable to get altitude. Error code: %u", sensorName, error);
        return false;
    }
    LOG_INFO("%s: Sensor ambient pressure: %u", sensorName, ambientPressure);

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
        LOG_ERROR("%s: Unable to set altitude. Error code: %u", sensorName, error);
        return false;
    }

    error = scd4x.persistSettings();
    if (error != SCD4X_NO_ERROR){
        LOG_ERROR("%s: Unable to make settings persistent. Error code: %u", sensorName, error);
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
        LOG_ERROR("%s: Unable to set altitude. Error code: %u", sensorName, error);
        return false;
    }

    // Sensirion doesn't indicate if this is necessary. We send it anyway
    error = scd4x.persistSettings();
    if (error != SCD4X_NO_ERROR){
        LOG_ERROR("%s: Unable to make settings persistent. Error code: %u", sensorName, error);
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

    LOG_INFO("%s: Requesting factory reset", sensorName);

    if (!stopMeasurement()) {
        return false;
    }

    error = scd4x.performFactoryReset();

    if (error != SCD4X_NO_ERROR){
        LOG_ERROR("%s: Unable to do factory reset. Error code: %u", sensorName, error);
        return false;
    }

    LOG_INFO("%s: Factory reset successful", sensorName);

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
bool SCD4XSensor::powerDown() {
    LOG_INFO("%s: Trying to send sensor to sleep", sensorName);

    if (sensorVariant != SCD4X_SENSOR_VARIANT_SCD41) {
        LOG_WARN("SCD4X: Can't send sensor to sleep. Incorrect variant. Ignoring");
        return true;
    }

    if (!stopMeasurement()) {
        return false;
    }

    if (scd4x.powerDown() != SCD4X_NO_ERROR) {
        LOG_ERROR("%s: Error trying to execute sleep()", sensorName);
        return false;
    }
    state = SCD4X_OFF;
    return true;
}

/**
* @brief Wake up sensor from sleep mode to idle mode (powerUp)
*
* From Sensirion SCD4X I2C Library.
*
* Wake up the sensor from sleep mode into idle mode. Note that the SCD4x
* does not acknowledge the wake_up command. The sensor's idle state after
* wake up can be verified by reading out the serial number.
*
* @note This command is only available for SCD41.
*/
bool SCD4XSensor::powerUp(){
    LOG_INFO("%s: Waking up", sensorName);

    if (scd4x.wakeUp() != SCD4X_NO_ERROR) {
        LOG_ERROR("%s: Error trying to execute wakeUp()", sensorName);
        return false;
    }
    state = SCD4X_IDLE;
    return true;
}

/**
* @brief Check if sensor is in measurement mode
*/
bool SCD4XSensor::isActive(){
    return state == SCD4X_MEASUREMENT;
}

/**
* @brief Start measurement mode
*/
uint32_t SCD4XSensor::wakeUp(){
    if (startMeasurement()) {
        co2MeasureStarted = getTime();
        return SCD4X_WARMUP_MS;
    }
    return 0;
}

/**
* @brief Stop measurement mode
*/
void SCD4XSensor::sleep(){
    stopMeasurement();
}

/**
* @brief Can sleep function
*
* Power consumption is very low on lowPower mode, modify this function if
* you still want to override this behaviour. Otherwise, sleep is disabled
* routinely in low power mode
*/
bool SCD4XSensor::canSleep(){
    return lowPower ? false : true;
}

int32_t SCD4XSensor::wakeUpTimeMs(){
    return SCD4X_WARMUP_MS;
}

int32_t SCD4XSensor::pendingForReadyMs()
{
    uint32_t now;
    now = getTime();
    uint32_t sinceCO2MeasureStarted = (now - co2MeasureStarted)*1000;
    LOG_DEBUG("%s: Since measure started: %ums", sensorName, sinceCO2MeasureStarted);

    if (sinceCO2MeasureStarted < SCD4X_WARMUP_MS) {
        LOG_INFO("%s: not enough time passed since starting measurement", sensorName);
        return SCD4X_WARMUP_MS - sinceCO2MeasureStarted;
    }
    return 0;
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
                LOG_DEBUG("%s: Requested factory reset", sensorName);
                this->factoryReset();
            } else {

                if (request->sensor_config.scd4x_config.has_set_asc) {
                    this->setASC(request->sensor_config.scd4x_config.set_asc);
                    if (request->sensor_config.scd4x_config.set_asc == false) {
                        LOG_DEBUG("%s: Request for FRC", sensorName);
                        if (request->sensor_config.scd4x_config.has_set_target_co2_conc) {
                            this->performFRC(request->sensor_config.scd4x_config.set_target_co2_conc);
                        } else {
                            // FRC requested but no target CO2 provided
                            LOG_ERROR("%s: target CO2 not provided", sensorName);
                            result = AdminMessageHandleResult::NOT_HANDLED;
                            break;
                        }
                    } else {
                        LOG_DEBUG("%s: Request for ASC", sensorName);
                        if (request->sensor_config.scd4x_config.has_set_target_co2_conc) {
                            LOG_DEBUG("%s: Request has target CO2", sensorName);
                            // TODO - Remove? see setASCBaseline function
                            this->setASCBaseline(request->sensor_config.scd4x_config.set_target_co2_conc);
                        } else {
                            LOG_DEBUG("%s: Request doesn't have target CO2", sensorName);
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