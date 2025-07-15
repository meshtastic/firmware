#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "SEN5XSensor.h"
#include "TelemetrySensor.h"
#include "FSCommon.h"
#include "SPILock.h"

SEN5XSensor::SEN5XSensor() : TelemetrySensor(meshtastic_TelemetrySensorType_SEN5X, "SEN5X") {}

bool SEN5XSensor::restoreClock(uint32_t currentClock){
#ifdef SEN5X_I2C_CLOCK_SPEED
    if (currentClock != SEN5X_I2C_CLOCK_SPEED){
        LOG_DEBUG("Restoring I2C clock to %uHz", currentClock);
        return bus->setClock(currentClock);
    }
    return true;
#endif
}

bool SEN5XSensor::getVersion()
{
    if (!sendCommand(SEN5X_GET_FIRMWARE_VERSION)){
        LOG_ERROR("SEN5X: Error sending version command");
        return false;
    }
    delay(20); // From Sensirion Arduino library

    uint8_t versionBuffer[12];
    size_t charNumber = readBuffer(&versionBuffer[0], 3);
    if (charNumber == 0) {
        LOG_ERROR("SEN5X: Error getting data ready flag value");
        return false;
    }

    firmwareVer = versionBuffer[0] + (versionBuffer[1] / 10);
    hardwareVer = versionBuffer[3] + (versionBuffer[4] / 10);
    protocolVer = versionBuffer[5] + (versionBuffer[6] / 10);

    LOG_INFO("SEN5X Firmware Version: %0.2f", firmwareVer);
    LOG_INFO("SEN5X Hardware Version: %0.2f", hardwareVer);
    LOG_INFO("SEN5X Protocol Version: %0.2f", protocolVer);

    return true;
}

bool SEN5XSensor::findModel()
{
    if (!sendCommand(SEN5X_GET_PRODUCT_NAME)) {
        LOG_ERROR("SEN5X: Error asking for product name");
        return false;
    }
    delay(50); // From Sensirion Arduino library

    const uint8_t nameSize = 48;
    uint8_t name[nameSize];
    size_t charNumber = readBuffer(&name[0], nameSize);
    if (charNumber == 0) {
        LOG_ERROR("SEN5X: Error getting device name");
        return false;
    }

    // We only check the last character that defines the model SEN5X
    switch(name[4])
    {
        case 48:
            model = SEN50;
            LOG_INFO("SEN5X: found sensor model SEN50");
            break;
        case 52:
            model = SEN54;
            LOG_INFO("SEN5X: found sensor model SEN54");
            break;
        case 53:
            model = SEN55;
            LOG_INFO("SEN5X: found sensor model SEN55");
            break;
    }

    return true;
}

bool SEN5XSensor::sendCommand(uint16_t command)
{
    uint8_t nothing;
    return sendCommand(command, &nothing, 0);
}

bool SEN5XSensor::sendCommand(uint16_t command, uint8_t* buffer, uint8_t byteNumber)
{
    // At least we need two bytes for the command
    uint8_t bufferSize = 2;

    // Add space for CRC bytes (one every two bytes)
    if (byteNumber > 0) bufferSize += byteNumber + (byteNumber / 2);

    uint8_t toSend[bufferSize];
    uint8_t i = 0;
    toSend[i++] = static_cast<uint8_t>((command & 0xFF00) >> 8);
    toSend[i++] = static_cast<uint8_t>((command & 0x00FF) >> 0);

    // Prepare buffer with CRC every third byte
    uint8_t bi = 0;
    if (byteNumber > 0) {
        while (bi < byteNumber) {
            toSend[i++] = buffer[bi++];
            toSend[i++] = buffer[bi++];
            uint8_t calcCRC = sen5xCRC(&buffer[bi - 2]);
            toSend[i++] = calcCRC;
        }
    }

#ifdef SEN5X_I2C_CLOCK_SPEED
    uint32_t currentClock;
    currentClock = bus->getClock();
    if (currentClock != SEN5X_I2C_CLOCK_SPEED){
        LOG_DEBUG("Changing I2C clock to %u", SEN5X_I2C_CLOCK_SPEED);
        bus->setClock(SEN5X_I2C_CLOCK_SPEED);
    }
#endif

    // Transmit the data
    LOG_INFO("Beginning connection to SEN5X: 0x%x", address);
    bus->beginTransmission(address);
    size_t writtenBytes = bus->write(toSend, bufferSize);
    uint8_t i2c_error = bus->endTransmission();

    restoreClock(currentClock);

    if (writtenBytes != bufferSize) {
        LOG_ERROR("SEN5X: Error writting on I2C bus");
        return false;
    }

    if (i2c_error != 0) {
        LOG_ERROR("SEN5X: Error on I2c communication: %x", i2c_error);
        return false;
    }
    return true;
}

uint8_t SEN5XSensor::readBuffer(uint8_t* buffer, uint8_t byteNumber)
{
#ifdef SEN5X_I2C_CLOCK_SPEED
    uint32_t currentClock;
    currentClock = bus->getClock();
    if (currentClock != SEN5X_I2C_CLOCK_SPEED){
        LOG_DEBUG("Changing I2C clock to %u", SEN5X_I2C_CLOCK_SPEED);
        bus->setClock(SEN5X_I2C_CLOCK_SPEED);
    }
#endif

    size_t readBytes = bus->requestFrom(address, byteNumber);
    if (readBytes != byteNumber) {
        LOG_ERROR("SEN5X: Error reading I2C bus");
        return 0;
    }

    uint8_t i = 0;
    uint8_t receivedBytes = 0;
    while (readBytes > 0) {
        buffer[i++] = bus->read(); // Just as a reminder: i++ returns i and after that increments.
        buffer[i++] = bus->read();
        uint8_t recvCRC = bus->read();
        uint8_t calcCRC = sen5xCRC(&buffer[i - 2]);
        if (recvCRC != calcCRC) {
            LOG_ERROR("SEN5X: Checksum error while receiving msg");
            return 0;
        }
        readBytes -=3;
        receivedBytes += 2;
    }
    restoreClock(currentClock);
    return receivedBytes;
}

uint8_t SEN5XSensor::sen5xCRC(uint8_t* buffer)
{
    // This code is based on Sensirion's own implementation https://github.com/Sensirion/arduino-core/blob/41fd02cacf307ec4945955c58ae495e56809b96c/src/SensirionCrc.cpp
    uint8_t crc = 0xff;

    for (uint8_t i=0; i<2; i++){

        crc ^= buffer[i];

        for (uint8_t bit=8; bit>0; bit--) {
            if (crc & 0x80)
                crc = (crc << 1) ^ 0x31;
            else
                crc = (crc << 1);
        }
    }

    return crc;
}

bool SEN5XSensor::I2Cdetect(TwoWire *_Wire, uint8_t address)
{
    _Wire->beginTransmission(address);
    byte error = _Wire->endTransmission();

    if (error == 0) return true;
    else return false;
}

bool SEN5XSensor::idle()
{
    // In continous mode we don't sleep
    if (continousMode || forcedContinousMode) {
        LOG_ERROR("SEN5X: Not going to idle mode, we are in continous mode!!");
        return false;
    }
    // TODO - Get VOC state before going to idle mode
    // vocStateFromSensor();

    if (!sendCommand(SEN5X_STOP_MEASUREMENT)) {
        LOG_ERROR("SEN5X: Error stoping measurement");
        return false;
    }
    delay(200); // From Sensirion Arduino library

    LOG_INFO("SEN5X: Stop measurement mode");

    state = SEN5X_IDLE;
    measureStarted = 0;

    return true;
}

void SEN5XSensor::loadCleaningState()
{
#ifdef FSCom
    spiLock->lock();
    auto file = FSCom.open(sen5XCleaningFileName, FILE_O_READ);
    if (file) {
        file.read();
        file.close();
        LOG_INFO("Cleaning state %u read for %s read from %s", lastCleaning, sensorName, sen5XCleaningFileName);
    } else {
        LOG_INFO("No %s state found (File: %s)", sensorName, sen5XCleaningFileName);
    }
    spiLock->unlock();
#else
    LOG_ERROR("ERROR: Filesystem not implemented");
#endif
}

void SEN5XSensor::updateCleaningState()
{
#ifdef FSCom
    spiLock->lock();

    if (FSCom.exists(sen5XCleaningFileName) && !FSCom.remove(sen5XCleaningFileName)) {
        LOG_WARN("Can't remove old state file");
    }
    auto file = FSCom.open(sen5XCleaningFileName, FILE_O_WRITE);
    if (file) {
        LOG_INFO("Save cleaning state %u for %s to %s", lastCleaning, sensorName, sen5XCleaningFileName);
        file.write(lastCleaning);
        file.flush();
        file.close();
    } else {
        LOG_INFO("Can't write %s state (File: %s)", sensorName, sen5XCleaningFileName);
    }

    spiLock->unlock();
#else
    LOG_ERROR("ERROR: Filesystem not implemented");
#endif
}

bool SEN5XSensor::isActive(){
    return state == SEN5X_MEASUREMENT || state == SEN5X_MEASUREMENT_2;
}

uint32_t SEN5XSensor::wakeUp(){
    LOG_INFO("SEN5X: Attempting to wakeUp sensor");
    if (!sendCommand(SEN5X_START_MEASUREMENT)) {
        LOG_INFO("SEN5X: Error starting measurement");
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    }
    delay(50); // From Sensirion Arduino library

    LOG_INFO("SEN5X: Setting measurement mode");
    uint32_t now;
    now = getTime();
    measureStarted = now;
    state = SEN5X_MEASUREMENT;
    if (state == SEN5X_MEASUREMENT)
        LOG_INFO("SEN5X: Started measurement mode");
    return SEN5X_WARMUP_MS_1;
}

bool SEN5XSensor::startCleaning()
{
    state = SEN5X_CLEANING;

    // Note that this command can only be run when the sensor is in measurement mode
    if (!sendCommand(SEN5X_START_MEASUREMENT)) {
        LOG_ERROR("SEN5X: Error starting measurment mode");
        return false;
    }
    delay(50); // From Sensirion Arduino library

    if (!sendCommand(SEN5X_START_FAN_CLEANING)) {
        LOG_ERROR("SEN5X: Error starting fan cleaning");
        return false;
    }
    delay(20); // From Sensirion Arduino library

    // This message will be always printed so the user knows the device it's not hung
    LOG_INFO("SEN5X: Started fan cleaning it will take 10 seconds...");

    uint16_t started = millis();
    while (millis() - started < 10500) {
        // Serial.print(".");
        delay(500);
    }
    LOG_INFO(" Cleaning done!!");

    // Save timestamp in flash so we know when a week has passed
    uint32_t now;
    now = getTime();
    lastCleaning = now;
    updateCleaningState();

    idle();
    return true;
}

int32_t SEN5XSensor::runOnce()
{
    state = SEN5X_NOT_DETECTED;
    LOG_INFO("Init sensor: %s", sensorName);
    if (!hasSensor()) {
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    }

    bus = nodeTelemetrySensorsMap[sensorType].second;
    address = (uint8_t)nodeTelemetrySensorsMap[sensorType].first;

    delay(50); // without this there is an error on the deviceReset function

    if (!sendCommand(SEN5X_RESET)) {
        LOG_ERROR("SEN5X: Error reseting device");
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    }
    delay(200); // From Sensirion Arduino library

    if (!findModel()) {
        LOG_ERROR("SEN5X: error finding sensor model");
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    }

    // Check if firmware version allows The direct switch between Measurement and RHT/Gas-Only Measurement mode
    if (!getVersion()) return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    if (firmwareVer < 2) {
        LOG_ERROR("SEN5X: error firmware is too old and will not work with this implementation");
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    }
    delay(200); // From Sensirion Arduino library

    // Detection succeeded
    state = SEN5X_IDLE;
    status = 1;
    LOG_INFO("SEN5X Enabled");

    // Check if it is time to do a cleaning
    // TODO - this is not currently working as intended - always reading 0 from the file. We should probably make a unified approach for both the cleaning and the VOCstate
    loadCleaningState();
    LOG_INFO("Last cleaning time: %u", lastCleaning);
    if (lastCleaning) {
        LOG_INFO("Last cleaning is valid");

        uint32_t now;
        now = getTime();
        LOG_INFO("Current time %us", now);
        uint32_t passed = now - lastCleaning;
        LOG_INFO("Elapsed time since last cleaning: %us", passed);

        if (passed > ONE_WEEK_IN_SECONDS && (now > 1514764800)) {       // If current date greater than 01/01/2018 (validity check)
            LOG_INFO("SEN5X: More than a week since las cleaning, cleaning...");
            startCleaning();
        } else {
            LOG_INFO("Last cleaning date (in epoch): %u", lastCleaning);
        }
    } else {
        LOG_INFO("Last cleaning is not valid");
        // We asume the device has just been updated or it is new, so no need to trigger a cleaning.
        // Just save the timestamp to do a cleaning one week from now.
        lastCleaning = getTime();
        updateCleaningState();
        LOG_INFO("SEN5X: No valid last cleaning date found, saving it now: %u", lastCleaning);
    }

    // TODO - Should wakeUp happen here?
    return initI2CSensor();
}

void SEN5XSensor::setup()
{
}

bool SEN5XSensor::readValues()
{
    if (!sendCommand(SEN5X_READ_VALUES)){
        LOG_ERROR("SEN5X: Error sending read command");
        return false;
    }
    LOG_DEBUG("SEN5X: Reading PM Values");
    delay(20); // From Sensirion Arduino library

    uint8_t dataBuffer[24];
    size_t receivedNumber = readBuffer(&dataBuffer[0], 24);
    if (receivedNumber == 0) {
        LOG_ERROR("SEN5X: Error getting values");
        return false;
    }

    // First get the integers
    uint16_t uint_pM1p0        = static_cast<uint16_t>((dataBuffer[0]  << 8) | dataBuffer[1]);
    uint16_t uint_pM2p5        = static_cast<uint16_t>((dataBuffer[2]  << 8) | dataBuffer[3]);
    uint16_t uint_pM4p0        = static_cast<uint16_t>((dataBuffer[4]  << 8) | dataBuffer[5]);
    uint16_t uint_pM10p0       = static_cast<uint16_t>((dataBuffer[6]  << 8) | dataBuffer[7]);
    int16_t  int_humidity      = static_cast<int16_t>((dataBuffer[8]   << 8) | dataBuffer[9]);
    int16_t  int_temperature   = static_cast<int16_t>((dataBuffer[10]  << 8) | dataBuffer[11]);
    int16_t  int_vocIndex      = static_cast<int16_t>((dataBuffer[12]  << 8) | dataBuffer[13]);
    int16_t  int_noxIndex      = static_cast<int16_t>((dataBuffer[14]  << 8) | dataBuffer[15]);

    // TODO we should check if values are NAN before converting them
    // convert them based on Sensirion Arduino lib
    // TODO - Change based on the type of final values
    sen5xmeasurement.pM1p0          = uint_pM1p0      / 10.0f;
    sen5xmeasurement.pM2p5          = uint_pM2p5      / 10.0f;
    sen5xmeasurement.pM4p0          = uint_pM4p0      / 10.0f;
    sen5xmeasurement.pM10p0         = uint_pM10p0     / 10.0f;
    sen5xmeasurement.humidity       = int_humidity    / 100.0f;
    sen5xmeasurement.temperature    = int_temperature / 200.0f;
    sen5xmeasurement.vocIndex       = int_vocIndex    / 10.0f;
    sen5xmeasurement.noxIndex       = int_noxIndex    / 10.0f;

    // TODO - change depending on the final values
    LOG_DEBUG("Got: pM1p0=%.2f, pM2p5=%.2f, pM4p0=%.2f, pM10p0=%.2f",
                sen5xmeasurement.pM1p0, sen5xmeasurement.pM2p5,
                sen5xmeasurement.pM4p0, sen5xmeasurement.pM10p0);

    return true;
}

bool SEN5XSensor::readPnValues()
{
    if (!sendCommand(SEN5X_READ_PM_VALUES)){
        LOG_ERROR("SEN5X: Error sending read command");
        return false;
    }
    LOG_DEBUG("SEN5X: Reading PN Values");
    delay(20); // From Sensirion Arduino library

    uint8_t dataBuffer[30];
    size_t receivedNumber = readBuffer(&dataBuffer[0], 30);
    if (receivedNumber == 0) {
        LOG_ERROR("SEN5X: Error getting PN values");
        return false;
    }

    // First get the integers
    // uint16_t uint_pM1p0   = static_cast<uint16_t>((dataBuffer[0]   << 8) | dataBuffer[1]);
    // uint16_t uint_pM2p5   = static_cast<uint16_t>((dataBuffer[2]   << 8) | dataBuffer[3]);
    // uint16_t uint_pM4p0   = static_cast<uint16_t>((dataBuffer[4]   << 8) | dataBuffer[5]);
    // uint16_t uint_pM10p0  = static_cast<uint16_t>((dataBuffer[6]   << 8) | dataBuffer[7]);
    uint16_t uint_pN0p5   = static_cast<uint16_t>((dataBuffer[8]   << 8) | dataBuffer[9]);
    uint16_t uint_pN1p0   = static_cast<uint16_t>((dataBuffer[10]  << 8) | dataBuffer[11]);
    uint16_t uint_pN2p5   = static_cast<uint16_t>((dataBuffer[12]  << 8) | dataBuffer[13]);
    uint16_t uint_pN4p0   = static_cast<uint16_t>((dataBuffer[14]  << 8) | dataBuffer[15]);
    uint16_t uint_pN10p0  = static_cast<uint16_t>((dataBuffer[16]  << 8) | dataBuffer[17]);
    uint16_t uint_tSize   = static_cast<uint16_t>((dataBuffer[18]  << 8) | dataBuffer[19]);

    // Convert them based on Sensirion Arduino lib
    // sen5xmeasurement.pM1p0   = uint_pM1p0  / 10.0f;
    // sen5xmeasurement.pM2p5   = uint_pM2p5  / 10.0f;
    // sen5xmeasurement.pM4p0   = uint_pM4p0  / 10.0f;
    // sen5xmeasurement.pM10p0  = uint_pM10p0 / 10.0f;
    sen5xmeasurement.pN0p5   = uint_pN0p5  / 10;
    sen5xmeasurement.pN1p0   = uint_pN1p0  / 10;
    sen5xmeasurement.pN2p5   = uint_pN2p5  / 10;
    sen5xmeasurement.pN4p0   = uint_pN4p0  / 10;
    sen5xmeasurement.pN10p0  = uint_pN10p0 / 10;
    sen5xmeasurement.tSize   = uint_tSize  / 1000.0f;

    // Convert PN readings from #/cm3 to #/0.1l
    // TODO - Decide if those units are right
    // TODO Remove accumuluative values:
    // https://github.com/fablabbcn/smartcitizen-kit-2x/issues/85
    sen5xmeasurement.pN0p5  *= 100;
    sen5xmeasurement.pN1p0  *= 100;
    sen5xmeasurement.pN2p5  *= 100;
    sen5xmeasurement.pN4p0  *= 100;
    sen5xmeasurement.pN10p0 *= 100;
    sen5xmeasurement.tSize  *= 100;

    // TODO - Change depending on the final values
    LOG_DEBUG("Got: pN0p5=%u, pN1p0=%u, pN2p5=%u, pN4p0=%u, pN10p0=%u, tSize=%.2f",
                sen5xmeasurement.pN0p5, sen5xmeasurement.pN1p0,
                sen5xmeasurement.pN2p5, sen5xmeasurement.pN4p0,
                sen5xmeasurement.pN10p0, sen5xmeasurement.tSize
                );

    return true;
}

// TODO - Decide if we want to have this here or not
// bool SEN5XSensor::readRawValues()
// {
//     if (!sendCommand(SEN5X_READ_RAW_VALUES)){
//         LOG_ERROR("SEN5X: Error sending read command");
//         return false;
//     }
//     delay(20); // From Sensirion Arduino library

//     uint8_t dataBuffer[12];
//     size_t receivedNumber = readBuffer(&dataBuffer[0], 12);
//     if (receivedNumber == 0) {
//         LOG_ERROR("SEN5X: Error getting Raw values");
//         return false;
//     }

//     // Get values
//     rawHumidity     = static_cast<int16_t>((dataBuffer[0]   << 8) | dataBuffer[1]);
//     rawTemperature  = static_cast<int16_t>((dataBuffer[2]   << 8) | dataBuffer[3]);
//     rawVoc          = static_cast<uint16_t>((dataBuffer[4]  << 8) | dataBuffer[5]);
//     rawNox          = static_cast<uint16_t>((dataBuffer[6]  << 8) | dataBuffer[7]);

//     return true;
// }

uint8_t SEN5XSensor::getMeasurements()
{
    // Try to get new data
    if (!sendCommand(SEN5X_READ_DATA_READY)){
        LOG_ERROR("SEN5X: Error sending command data ready flag");
        return 2;
    }
    delay(20); // From Sensirion Arduino library

    uint8_t dataReadyBuffer[3];
    size_t charNumber = readBuffer(&dataReadyBuffer[0], 3);
    if (charNumber == 0) {
        LOG_ERROR("SEN5X: Error getting device version value");
        return 2;
    }

    bool data_ready = dataReadyBuffer[1];

    if (!data_ready) {
        LOG_INFO("SEN5X: Data is not ready");
        return 1;
    }

    if(!readValues()) {
        LOG_ERROR("SEN5X: Error getting readings");
        return 2;
    }

    if(!readPnValues()) {
        LOG_ERROR("SEN5X: Error getting PM readings");
        return 2;
    }

    // if(!readRawValues()) {
    //     LOG_ERROR("SEN5X: Error getting Raw readings");
    //     return 2;
    // }

    return 0;
}

bool SEN5XSensor::getMetrics(meshtastic_Telemetry *measurement)
{
    LOG_INFO("SEN5X: Attempting to get metrics");
    if (!isActive()){
        LOG_INFO("SEN5X: not in measurement mode");
        return false;
    }

    uint8_t response;
    response = getMeasurements();

    if (response == 0) {
        measurement->variant.air_quality_metrics.has_pm10_standard = true;
        measurement->variant.air_quality_metrics.pm10_standard = sen5xmeasurement.pM1p0;
        measurement->variant.air_quality_metrics.has_pm25_standard = true;
        measurement->variant.air_quality_metrics.pm25_standard = sen5xmeasurement.pM2p5;
        measurement->variant.air_quality_metrics.has_pm40_standard = true;
        measurement->variant.air_quality_metrics.pm40_standard = sen5xmeasurement.pM4p0;
        measurement->variant.air_quality_metrics.has_pm100_standard = true;
        measurement->variant.air_quality_metrics.pm100_standard = sen5xmeasurement.pM10p0;

        measurement->variant.air_quality_metrics.has_particles_05um = true;
        measurement->variant.air_quality_metrics.particles_05um = sen5xmeasurement.pN0p5;
        measurement->variant.air_quality_metrics.has_particles_10um = true;
        measurement->variant.air_quality_metrics.particles_10um = sen5xmeasurement.pN1p0;
        measurement->variant.air_quality_metrics.has_particles_25um = true;
        measurement->variant.air_quality_metrics.particles_25um = sen5xmeasurement.pN2p5;
        measurement->variant.air_quality_metrics.has_particles_40um = true;
        measurement->variant.air_quality_metrics.particles_40um = sen5xmeasurement.pN4p0;
        measurement->variant.air_quality_metrics.has_particles_100um = true;
        measurement->variant.air_quality_metrics.particles_100um = sen5xmeasurement.pN10p0;

        if (model == SEN54 || model == SEN55) {
            measurement->variant.air_quality_metrics.has_pm_humidity = true;
            measurement->variant.air_quality_metrics.pm_humidity = sen5xmeasurement.humidity;
            measurement->variant.air_quality_metrics.has_pm_temperature = true;
            measurement->variant.air_quality_metrics.pm_temperature = sen5xmeasurement.temperature;
            measurement->variant.air_quality_metrics.has_pm_nox_idx = true;
            measurement->variant.air_quality_metrics.pm_nox_idx = sen5xmeasurement.noxIndex;
        }

        if (model == SEN55) {
            measurement->variant.air_quality_metrics.has_pm_voc_idx = true;
            measurement->variant.air_quality_metrics.pm_voc_idx = sen5xmeasurement.vocIndex;
        }
        return true;
    } else if (response == 1) {
        // TODO return because data was not ready yet
        // Should this return false?
        return false;
    } else if (response == 2) {
        // Return with error for non-existing data
        return false;
    }

    return true;
}

#endif