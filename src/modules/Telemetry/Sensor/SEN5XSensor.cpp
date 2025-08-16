#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "SEN5XSensor.h"
#include "TelemetrySensor.h"
#include "FSCommon.h"
#include "SPILock.h"
#include "SafeFile.h"
#include <pb_decode.h>
#include <pb_encode.h>

SEN5XSensor::SEN5XSensor() : TelemetrySensor(meshtastic_TelemetrySensorType_SEN5X, "SEN5X") {}

bool SEN5XSensor::restoreClock(uint32_t currentClock){
#ifdef SEN5X_I2C_CLOCK_SPEED
    if (currentClock != SEN5X_I2C_CLOCK_SPEED){
        // LOG_DEBUG("Restoring I2C clock to %uHz", currentClock);
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
    delay(20); // From Sensirion Datasheet

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
    delay(50); // From Sensirion Datasheet

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
        // LOG_DEBUG("Changing I2C clock to %u", SEN5X_I2C_CLOCK_SPEED);
        bus->setClock(SEN5X_I2C_CLOCK_SPEED);
    }
#endif

    // Transmit the data
    // LOG_DEBUG("Beginning connection to SEN5X: 0x%x. Size: %u", address, bufferSize);
    // Note: this is necessary to allow for long-buffers
    delay(20);
    bus->beginTransmission(address);
    size_t writtenBytes = bus->write(toSend, bufferSize);
    uint8_t i2c_error = bus->endTransmission();

#ifdef SEN5X_I2C_CLOCK_SPEED
    restoreClock(currentClock);
#endif

    if (writtenBytes != bufferSize) {
        LOG_ERROR("SEN5X: Error writting on I2C bus");
        return false;
    }

    if (i2c_error != 0) {
        LOG_ERROR("SEN5X: Error on I2C communication: %x", i2c_error);
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
        // LOG_DEBUG("Changing I2C clock to %u", SEN5X_I2C_CLOCK_SPEED);
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
#ifdef SEN5X_I2C_CLOCK_SPEED
    restoreClock(currentClock);
#endif

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


    // Get VOC state before going to idle mode
    if (vocStateFromSensor()) {
        // TODO Should this be saved with saveState()?
        // It so, we can consider not saving it when rebooting as
        // we would have likely saved it recently

        // Check if we have time, and store it
        uint32_t now;  // If time is RTCQualityNone, it will return zero
        now = getValidTime(RTCQuality::RTCQualityDevice);

        if (now) {
            vocTime = now;
            vocValid = true;
            // saveState();
        }
    } else {
        vocValid = false;
    }

    if (!sendCommand(SEN5X_STOP_MEASUREMENT)) {
        LOG_ERROR("SEN5X: Error stoping measurement");
        return false;
    }
    delay(200); // From Sensirion Datasheet

    LOG_INFO("SEN5X: Stop measurement mode");

    state = SEN5X_IDLE;
    measureStarted = 0;
    return true;
}

bool SEN5XSensor::vocStateToSensor()
{
    if (model != SEN55){
        return true;
    }

    if (!sendCommand(SEN5X_STOP_MEASUREMENT)) {
        LOG_ERROR("SEN5X: Error stoping measurement");
        return false;
    }
    delay(200); // From Sensirion Datasheet

    LOG_DEBUG("SEN5X: Sending VOC state to sensor");
    LOG_DEBUG("[%u, %u, %u, %u, %u, %u, %u, %u]",
        vocState[0],vocState[1], vocState[2], vocState[3],
        vocState[4],vocState[5], vocState[6], vocState[7]);

    // Note: send command already takes into account the CRC
    // buffer size increment needed
    if (!sendCommand(SEN5X_RW_VOCS_STATE, vocState, SEN5X_VOC_STATE_BUFFER_SIZE)){
        LOG_ERROR("SEN5X: Error sending VOC's state command'");
        return false;
    }

    return true;
}

bool SEN5XSensor::vocStateFromSensor()
{
    if (model != SEN55){
        return true;
    }

    LOG_INFO("SEN5X: Getting VOC state from sensor");
    //  Ask VOCs state from the sensor
    if (!sendCommand(SEN5X_RW_VOCS_STATE)){
        LOG_ERROR("SEN5X: Error sending VOC's state command'");
        return false;
    }

    delay(20); // From Sensirion Datasheet

    // Retrieve the data
    // Allocate buffer to account for CRC
    uint8_t vocBuffer[SEN5X_VOC_STATE_BUFFER_SIZE + (SEN5X_VOC_STATE_BUFFER_SIZE / 2)];
    size_t receivedNumber = readBuffer(&vocBuffer[0], SEN5X_VOC_STATE_BUFFER_SIZE + (SEN5X_VOC_STATE_BUFFER_SIZE / 2));
    delay(20); // From Sensirion Datasheet

    if (receivedNumber == 0) {
        LOG_DEBUG("SEN5X: Error getting VOC's state");
        return false;
    }

    vocState[0] = vocBuffer[0];
    vocState[1] = vocBuffer[1];
    vocState[2] = vocBuffer[3];
    vocState[3] = vocBuffer[4];
    vocState[4] = vocBuffer[6];
    vocState[5] = vocBuffer[7];
    vocState[6] = vocBuffer[9];
    vocState[7] = vocBuffer[10];

    // Print the state (if debug is on)
    LOG_DEBUG("SEN5X: VOC state retrieved from sensor");
    LOG_DEBUG("[%u, %u, %u, %u, %u, %u, %u, %u]",
        vocState[0],vocState[1], vocState[2], vocState[3],
        vocState[4],vocState[5], vocState[6], vocState[7]);

    return true;
}

bool SEN5XSensor::loadState()
{
#ifdef FSCom
    spiLock->lock();
    auto file = FSCom.open(sen5XStateFileName, FILE_O_READ);
    bool okay = false;
    if (file) {
        LOG_INFO("%s state read from %s", sensorName, sen5XStateFileName);
        pb_istream_t stream = {&readcb, &file, meshtastic_SEN5XState_size};
        if (!pb_decode(&stream, &meshtastic_SEN5XState_msg, &sen5xstate)) {
            LOG_ERROR("Error: can't decode protobuf %s", PB_GET_ERROR(&stream));
        } else {
            lastCleaning = sen5xstate.last_cleaning_time;
            lastCleaningValid = sen5xstate.last_cleaning_valid;
            // Unpack state
            vocState[7] = (uint8_t)(sen5xstate.voc_state >> 56);
            vocState[6] = (uint8_t)(sen5xstate.voc_state >> 48);
            vocState[5] = (uint8_t)(sen5xstate.voc_state >> 40);
            vocState[4] = (uint8_t)(sen5xstate.voc_state >> 32);
            vocState[3] = (uint8_t)(sen5xstate.voc_state >> 24);
            vocState[2] = (uint8_t)(sen5xstate.voc_state >> 16);
            vocState[1] = (uint8_t)(sen5xstate.voc_state >> 8);
            vocState[0] = (uint8_t)sen5xstate.voc_state;

            vocTime = sen5xstate.voc_time;
            vocValid = sen5xstate.voc_valid;
            okay = true;
        }
        file.close();
    } else {
        LOG_INFO("No %s state found (File: %s)", sensorName, sen5XStateFileName);
    }
    spiLock->unlock();
    return okay;
#else
    LOG_ERROR("SEN5X: ERROR - Filesystem not implemented");
#endif
}

bool SEN5XSensor::saveState()
{
    // TODO - This should be called before a reboot
    // is there a way to get notified?
#ifdef FSCom
    auto file = SafeFile(sen5XStateFileName);

    sen5xstate.last_cleaning_time = lastCleaning;
    sen5xstate.last_cleaning_valid = lastCleaningValid;

    // Unpack state (12 bytes in two parts)
    sen5xstate.voc_state = ((uint64_t) vocState[7] << 56) |
        ((uint64_t) vocState[6] << 48) |
        ((uint64_t) vocState[5] << 40) |
        ((uint64_t) vocState[4] << 32) |
        ((uint32_t) vocState[3] << 24) |
        ((uint32_t) vocState[2] << 16) |
        ((uint32_t) vocState[1] << 8) |
        vocState[0];

    LOG_INFO("sen5xstate.voc_state %i", sen5xstate.voc_state);

    sen5xstate.voc_time = vocTime;
    sen5xstate.voc_valid = vocValid;

    bool okay = false;

    LOG_INFO("%s: state write to %s", sensorName, sen5XStateFileName);
    pb_ostream_t stream = {&writecb, static_cast<Print *>(&file), meshtastic_SEN5XState_size};

    if (!pb_encode(&stream, &meshtastic_SEN5XState_msg, &sen5xstate)) {
        LOG_ERROR("Error: can't encode protobuf %s", PB_GET_ERROR(&stream));
    } else {
        okay = true;
    }

    okay &= file.close();

    if (okay)
        LOG_INFO("%s: state write to %s successful", sensorName, sen5XStateFileName);

    return okay;
#else
    LOG_ERROR("%s: ERROR - Filesystem not implemented", sensorName);
#endif
}

bool SEN5XSensor::isActive(){
    return state == SEN5X_MEASUREMENT || state == SEN5X_MEASUREMENT_2;
}

uint32_t SEN5XSensor::wakeUp(){
    // LOG_INFO("SEN5X: Attempting to wakeUp sensor");

    // From the datasheet
    // By default, the VOC algorithm resets its state to initial
    // values each time a measurement is started,
    // even if the measurement was stopped only for a short
    // time. So, the VOC index output value needs a long time
    // until it is stable again. This can be avoided by
    // restoring the previously memorized algorithm state before
    // starting the measure mode

    // TODO - This needs to be tested
    // In SC, the sensor is operated in contionuous mode if
    // VOCs are present, increasing battery consumption
    // A different approach should be possible as stated on the
    // datasheet (see above)
    // uint32_t now, passed;
    // now = getValidTime(RTCQuality::RTCQualityDevice);
    // passed = now - vocTime; //in seconds
    // // Check if state is recent, less than 10 minutes (600 seconds)
    // if ((passed < SEN5X_VOC_VALID_TIME) && (now > SEN5X_VOC_VALID_DATE) && vocValid) {
    //     if (!vocStateToSensor()){
    //         LOG_ERROR("SEN5X: Sending VOC state to sensor failed");
    //     }
    // } else {
    //     LOG_DEBUG("SEN5X: No valid VOC state found. Ignoring");
    // }

    if (!sendCommand(SEN5X_START_MEASUREMENT)) {
        LOG_ERROR("SEN5X: Error starting measurement");
        // TODO - what should this return??
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    }
    delay(50); // From Sensirion Datasheet

    // LOG_INFO("SEN5X: Setting measurement mode");
    // TODO - This is currently "problematic"
    // If time is updated in between reads, there is no way to
    // keep track of how long it has passed
    measureStarted = getTime();
    state = SEN5X_MEASUREMENT;
    if (state == SEN5X_MEASUREMENT)
        LOG_INFO("SEN5X: Started measurement mode");
    return SEN5X_WARMUP_MS_1;
}

bool SEN5XSensor::startCleaning()
{
    // Note: we only should enter here if we have a valid RTC with at least
    // RTCQuality::RTCQualityDevice
    state = SEN5X_CLEANING;

    // Note that cleaning command can only be run when the sensor is in measurement mode
    if (!sendCommand(SEN5X_START_MEASUREMENT)) {
        LOG_ERROR("SEN5X: Error starting measurment mode");
        return false;
    }
    delay(50); // From Sensirion Datasheet

    if (!sendCommand(SEN5X_START_FAN_CLEANING)) {
        LOG_ERROR("SEN5X: Error starting fan cleaning");
        return false;
    }
    delay(20); // From Sensirion Datasheet

    // This message will be always printed so the user knows the device it's not hung
    LOG_INFO("SEN5X: Started fan cleaning it will take 10 seconds...");

    uint16_t started = millis();
    while (millis() - started < 10500) {
        // Serial.print(".");
        delay(500);
    }
    LOG_INFO("SEN5X: Cleaning done!!");

    // Save timestamp in flash so we know when a week has passed
    uint32_t now;
    now = getValidTime(RTCQuality::RTCQualityDevice);
    // If time is not RTCQualityNone, it will return non-zero
    lastCleaning = now;
    lastCleaningValid = true;
    saveState();

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
    delay(200); // From Sensirion Datasheet

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
    delay(200); // From Sensirion Datasheet

    // Detection succeeded
    state = SEN5X_IDLE;
    status = 1;

    // Load state
    loadState();

    // Check if it is time to do a cleaning
    uint32_t now;
    int32_t passed;
    now = getValidTime(RTCQuality::RTCQualityDevice);
    // If time is not RTCQualityNone, it will return non-zero

    if (now) {
        if (lastCleaningValid) {

            passed = now - lastCleaning; // in seconds

            if (passed > ONE_WEEK_IN_SECONDS && (now > SEN5X_VOC_VALID_DATE)) {
                // If current date greater than 01/01/2018 (validity check)
                LOG_INFO("SEN5X: More than a week (%us) since last cleaning in epoch (%us). Trigger, cleaning...", passed, lastCleaning);
                startCleaning();
            } else {
                LOG_INFO("SEN5X: Cleaning not needed (%ds passed). Last cleaning date (in epoch): %us", passed, lastCleaning);
            }
        } else {
            // We assume the device has just been updated or it is new, so no need to trigger a cleaning.
            // Just save the timestamp to do a cleaning one week from now.
            // TODO - could we trigger this after getting time?
            // Otherwise, we will never trigger cleaning in some cases
            lastCleaning = now;
            lastCleaningValid = true;
            LOG_INFO("SEN5X: No valid last cleaning date found, saving it now: %us", lastCleaning);
            saveState();
        }
        if (model == SEN55) {
            if (!vocValid) {
                LOG_INFO("SEN5X: No valid VOC's state found");
            } else {
                passed = now - vocTime; //in seconds

                // Check if state is recent, less than 10 minutes (600 seconds)
                if (passed < SEN5X_VOC_VALID_TIME && (now > SEN5X_VOC_VALID_DATE)) {
                    // If current date greater than 01/01/2018 (validity check)
                    // Send it to the sensor
                    LOG_INFO("SEN5X: VOC state is valid and recent");
                    vocStateToSensor();
                } else {
                    LOG_INFO("SEN5X VOC state is to old or date is invalid");
                }
            }
        }

    } else {
        // TODO - Should this actually ignore? We could end up never cleaning...
        LOG_INFO("SEN5X: Not enough RTCQuality, ignoring saved state");
    }

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
    delay(20); // From Sensirion Datasheet

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

    // Convert values based on Sensirion Arduino lib
    sen5xmeasurement.pM1p0          = uint_pM1p0      / 10;
    sen5xmeasurement.pM2p5          = uint_pM2p5      / 10;
    sen5xmeasurement.pM4p0          = uint_pM4p0      / 10;
    sen5xmeasurement.pM10p0         = uint_pM10p0     / 10;
    sen5xmeasurement.humidity       = int_humidity    / 100.0f;
    sen5xmeasurement.temperature    = int_temperature / 200.0f;
    sen5xmeasurement.vocIndex       = int_vocIndex    / 10.0f;
    sen5xmeasurement.noxIndex       = int_noxIndex    / 10.0f;

    LOG_DEBUG("Got: pM1p0=%u, pM2p5=%u, pM4p0=%u, pM10p0=%u",
                sen5xmeasurement.pM1p0, sen5xmeasurement.pM2p5,
                sen5xmeasurement.pM4p0, sen5xmeasurement.pM10p0);

    return true;
}

bool SEN5XSensor::readPnValues(bool cumulative)
{
    if (!sendCommand(SEN5X_READ_PM_VALUES)){
        LOG_ERROR("SEN5X: Error sending read command");
        return false;
    }

    LOG_DEBUG("SEN5X: Reading PN Values");
    delay(20); // From Sensirion Datasheet

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

    // Convert values based on Sensirion Arduino lib
    sen5xmeasurement.pN0p5   = uint_pN0p5  / 10;
    sen5xmeasurement.pN1p0   = uint_pN1p0  / 10;
    sen5xmeasurement.pN2p5   = uint_pN2p5  / 10;
    sen5xmeasurement.pN4p0   = uint_pN4p0  / 10;
    sen5xmeasurement.pN10p0  = uint_pN10p0 / 10;
    sen5xmeasurement.tSize   = uint_tSize  / 1000.0f;

    // Convert PN readings from #/cm3 to #/0.1l
    sen5xmeasurement.pN0p5  *= 100;
    sen5xmeasurement.pN1p0  *= 100;
    sen5xmeasurement.pN2p5  *= 100;
    sen5xmeasurement.pN4p0  *= 100;
    sen5xmeasurement.pN10p0 *= 100;
    sen5xmeasurement.tSize  *= 100;

    // Remove accumuluative values:
    // https://github.com/fablabbcn/smartcitizen-kit-2x/issues/85
    if (!cumulative) {
        sen5xmeasurement.pN10p0 -= sen5xmeasurement.pN4p0;
        sen5xmeasurement.pN4p0 -= sen5xmeasurement.pN2p5;
        sen5xmeasurement.pN2p5 -= sen5xmeasurement.pN1p0;
        sen5xmeasurement.pN1p0 -= sen5xmeasurement.pN0p5;
    }

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
//     delay(20); // From Sensirion Datasheet

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
    delay(20); // From Sensirion Datasheet

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

    if(!readPnValues(false)) {
        LOG_ERROR("SEN5X: Error getting PM readings");
        return 2;
    }

    // if(!readRawValues()) {
    //     LOG_ERROR("SEN5X: Error getting Raw readings");
    //     return 2;
    // }

    return 0;
}

int32_t SEN5XSensor::pendingForReady(){
    uint32_t now;
    now = getTime();
    uint32_t sinceMeasureStarted = (now - measureStarted)*1000;
    LOG_DEBUG("SEN5X: Since measure started: %ums", sinceMeasureStarted);
    switch (state) {
        case SEN5X_MEASUREMENT: {

            if (sinceMeasureStarted < SEN5X_WARMUP_MS_1) {
                LOG_INFO("SEN5X: not enough time passed since starting measurement");
                return SEN5X_WARMUP_MS_1 - sinceMeasureStarted;
            }

            // Get PN values to check if we are above or below threshold
            readPnValues(true);

            // If the reading is low (the tyhreshold is in #/cm3) and second warmUp hasn't passed we return to come back later
            if ((sen5xmeasurement.pN4p0 / 100) < SEN5X_PN4P0_CONC_THD && sinceMeasureStarted < SEN5X_WARMUP_MS_2) {
                LOG_INFO("SEN5X: Concentration is low, we will ask again in the second warm up period");
                state = SEN5X_MEASUREMENT_2;
                // Report how many seconds are pending to cover the first warm up period
                return SEN5X_WARMUP_MS_2 - sinceMeasureStarted;
            }
            return 0;
        }
        case SEN5X_MEASUREMENT_2: {
            if (sinceMeasureStarted < SEN5X_WARMUP_MS_2) {
                // Report how many seconds are pending to cover the first warm up period
                return SEN5X_WARMUP_MS_2 - sinceMeasureStarted;
            }
            return 0;
        }
        default: {
            return -1;
        }
    }
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
        idle();
        return false;
    } else if (response == 2) {
        // Return with error for non-existing data
        idle();
        return false;
    }

    return true;
}

AdminMessageHandleResult SEN5XSensor::handleAdminMessage(const meshtastic_MeshPacket &mp, meshtastic_AdminMessage *request,
                                                           meshtastic_AdminMessage *response)
{
    AdminMessageHandleResult result;
    result = AdminMessageHandleResult::NOT_HANDLED;

    // TODO - Add admin command to set temperature offset
    // switch (request->which_payload_variant) {
    //     case meshtastic_AdminMessage_sensor_config_tag:
    //         if (!request->sensor_config.has_sen5x_config) {
    //             result = AdminMessageHandleResult::NOT_HANDLED;
    //             break;
    //         }

    //         // Check for temperature offset
    //         // if (request->sensor_config.sen5x_config.has_set_temperature) {
    //         //     this->setTemperature(request->sensor_config.sen5x_config.set_temperature);
    //         // }

    //         // result = AdminMessageHandleResult::HANDLED;
    //         result = AdminMessageHandleResult::NOT_HANDLED;
    //         break;

    //     default:
    //         result = AdminMessageHandleResult::NOT_HANDLED;
    // }

    return result;
}

#endif