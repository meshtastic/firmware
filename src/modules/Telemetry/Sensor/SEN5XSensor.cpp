#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_AIR_QUALITY_SENSOR

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "SEN5XSensor.h"
#include "../detect/reClockI2C.h"
#include "FSCommon.h"
#include "SPILock.h"
#include "SafeFile.h"
#include <pb_decode.h>
#include <pb_encode.h>
#include <float.h> // FLT_MAX

SEN5XSensor::SEN5XSensor()
    : TelemetrySensor(meshtastic_TelemetrySensorType_SEN5X, "SEN5X")
{
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
#ifdef CAN_RECLOCK_I2C
    uint32_t currentClock = reClockI2C(SEN5X_I2C_CLOCK_SPEED, _bus, false);
    if (currentClock != SEN5X_I2C_CLOCK_SPEED){
        LOG_WARN("%s can't be used at this clock speed (%u)", sensorName, currentClock);
        return false;
    }
#elif !HAS_SCREEN
    reClockI2C(SEN5X_I2C_CLOCK_SPEED, _bus, true);
#else
    LOG_WARN("%s can't be used at this clock speed, with a screen", sensorName);
    return false;
#endif /* CAN_RECLOCK_I2C */
#endif /* SEN5X_I2C_CLOCK_SPEED */


    // Transmit the data
    // LOG_DEBUG("Beginning connection to SEN5X: 0x%x. Size: %u", address, bufferSize);
    // Note: this is necessary to allow for long-buffers
    delay(20);
    _bus->beginTransmission(_address);
    size_t writtenBytes = _bus->write(toSend, bufferSize);
    uint8_t i2c_error = _bus->endTransmission();

#if defined(SEN5X_I2C_CLOCK_SPEED) && defined(CAN_RECLOCK_I2C)
    reClockI2C(currentClock, _bus, false);
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
#ifdef CAN_RECLOCK_I2C
    uint32_t currentClock = reClockI2C(SEN5X_I2C_CLOCK_SPEED, _bus, false);
    if (currentClock != SEN5X_I2C_CLOCK_SPEED){
        LOG_WARN("%s can't be used at this clock speed (%u)", sensorName, currentClock);
        return false;
    }
#elif !HAS_SCREEN
    reClockI2C(SEN5X_I2C_CLOCK_SPEED, _bus, true);
#else
    LOG_WARN("%s can't be used at this clock speed, with a screen", sensorName);
    return false;
#endif /* CAN_RECLOCK_I2C */
#endif /* SEN5X_I2C_CLOCK_SPEED */

    size_t readBytes = _bus->requestFrom(_address, byteNumber);
    if (readBytes != byteNumber) {
        LOG_ERROR("SEN5X: Error reading I2C bus");
        return 0;
    }

    uint8_t i = 0;
    uint8_t receivedBytes = 0;
    while (readBytes > 0) {
        buffer[i++] = _bus->read(); // Just as a reminder: i++ returns i and after that increments.
        buffer[i++] = _bus->read();
        uint8_t recvCRC = _bus->read();
        uint8_t calcCRC = sen5xCRC(&buffer[i - 2]);
        if (recvCRC != calcCRC) {
            LOG_ERROR("SEN5X: Checksum error while receiving msg");
            return 0;
        }
        readBytes -=3;
        receivedBytes += 2;
    }
#if defined(SEN5X_I2C_CLOCK_SPEED) && defined(CAN_RECLOCK_I2C)
    reClockI2C(currentClock, _bus, false);
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

void SEN5XSensor::sleep(){
    // TODO Check this works
    idle(true);
}

bool SEN5XSensor::idle(bool checkState)
{
    // From the datasheet:
    // By default, the VOC algorithm resets its state to initial
    // values each time a measurement is started,
    // even if the measurement was stopped only for a short
    // time. So, the VOC index output value needs a long time
    // until it is stable again. This can be avoided by
    // restoring the previously memorized algorithm state before
    // starting the measure mode

    if (checkState) {
        // If the stabilisation period is not passed for SEN54 or SEN55, don't go to idle
        if (model != SEN50) {
            // Get VOC state before going to idle mode
            vocValid = false;
            if (vocStateFromSensor()) {
                vocValid = vocStateValid();
                // Check if we have time, and store it
                uint32_t now;  // If time is RTCQualityNone, it will return zero
                now = getValidTime(RTCQuality::RTCQualityDevice);
                if (now) {
                    // Check if state is valid (non-zero)
                    vocTime = now;
                }
            }

            if (vocStateStable() && vocValid) {
                saveState();
            } else {
                LOG_INFO("SEN5X: Not stopping measurement, vocState is not stable yet!");
                return true;
            }
        }
    }

    if (!oneShotMode) {
        LOG_INFO("SEN5X: Not stopping measurement, continuous mode!");
        return true;
    }

    // Switch to low-power based on the model
    if (model == SEN50) {
        if (!sendCommand(SEN5X_STOP_MEASUREMENT)) {
            LOG_ERROR("SEN5X: Error stopping measurement");
            return false;
        }
        state = SEN5X_IDLE;
        LOG_INFO("SEN5X: Stop measurement mode");
    } else {
        if (!sendCommand(SEN5X_START_MEASUREMENT_RHT_GAS)) {
            LOG_ERROR("SEN5X: Error switching to RHT/Gas measurement");
            return false;
        }
        state = SEN5X_RHTGAS_ONLY;
        LOG_INFO("SEN5X: Switch to RHT/Gas only measurement mode");
    }

    delay(200); // From Sensirion Datasheet
    pmMeasureStarted = 0;
    return true;
}

bool SEN5XSensor::vocStateRecent(uint32_t now){
    if (now) {
        uint32_t passed = now - vocTime; //in seconds

        // Check if state is recent, less than 10 minutes (600 seconds)
        if (passed < SEN5X_VOC_VALID_TIME && (now > SEN5X_VOC_VALID_DATE)) {
            return true;
        }
    }
    return false;
}

bool SEN5XSensor::vocStateValid() {
    if (!vocState[0] && !vocState[1] && !vocState[2] && !vocState[3] &&
    !vocState[4] && !vocState[5] && !vocState[6] && !vocState[7]) {
        LOG_DEBUG("SEN5X: VOC state is all 0, invalid");
        return false;
    } else {
        LOG_DEBUG("SEN5X: VOC state is valid");
        return true;
    }
}

bool SEN5XSensor::vocStateToSensor()
{
    if (model == SEN50){
        return true;
    }

    if (!vocStateValid()) {
        LOG_INFO("SEN5X: VOC state is invalid, not sending");
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
    if (model == SEN50){
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
    // uint8_t vocBuffer[SEN5X_VOC_STATE_BUFFER_SIZE + (SEN5X_VOC_STATE_BUFFER_SIZE / 2)];
    size_t receivedNumber = readBuffer(&vocState[0], SEN5X_VOC_STATE_BUFFER_SIZE + (SEN5X_VOC_STATE_BUFFER_SIZE / 2));
    delay(20); // From Sensirion Datasheet

    if (receivedNumber == 0) {
        LOG_DEBUG("SEN5X: Error getting VOC's state");
        return false;
    }

    // vocState[0] = vocBuffer[0];
    // vocState[1] = vocBuffer[1];
    // vocState[2] = vocBuffer[3];
    // vocState[3] = vocBuffer[4];
    // vocState[4] = vocBuffer[6];
    // vocState[5] = vocBuffer[7];
    // vocState[6] = vocBuffer[9];
    // vocState[7] = vocBuffer[10];

    // Print the state (if debug is on)
    LOG_DEBUG("SEN5X: VOC state retrieved from sensor: [%u, %u, %u, %u, %u, %u, %u, %u]",
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
            oneShotMode = sen5xstate.one_shot_mode;

            if (model != SEN50) {
                vocTime = sen5xstate.voc_state_time;
                vocValid = sen5xstate.voc_state_valid;
                // Unpack state
                vocState[7] = (uint8_t)(sen5xstate.voc_state_array >> 56);
                vocState[6] = (uint8_t)(sen5xstate.voc_state_array >> 48);
                vocState[5] = (uint8_t)(sen5xstate.voc_state_array >> 40);
                vocState[4] = (uint8_t)(sen5xstate.voc_state_array >> 32);
                vocState[3] = (uint8_t)(sen5xstate.voc_state_array >> 24);
                vocState[2] = (uint8_t)(sen5xstate.voc_state_array >> 16);
                vocState[1] = (uint8_t)(sen5xstate.voc_state_array >> 8);
                vocState[0] = (uint8_t) sen5xstate.voc_state_array;
            }

            // LOG_DEBUG("Loaded lastCleaning %u", lastCleaning);
            // LOG_DEBUG("Loaded lastCleaningValid %u", lastCleaningValid);
            // LOG_DEBUG("Loaded oneShotMode %s", oneShotMode ? "true" : "false");
            // LOG_DEBUG("Loaded vocTime %u", vocTime);
            // LOG_DEBUG("Loaded [%u, %u, %u, %u, %u, %u, %u, %u]",
            // vocState[7], vocState[6], vocState[5], vocState[4], vocState[3], vocState[2], vocState[1], vocState[0]);
            // LOG_DEBUG("Loaded %svalid VOC state", vocValid ? "" : "in");

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
    // TODO - This should be called before a reboot for VOC index storage
    // is there a way to get notified?
#ifdef FSCom
    auto file = SafeFile(sen5XStateFileName);

    sen5xstate.last_cleaning_time = lastCleaning;
    sen5xstate.last_cleaning_valid = lastCleaningValid;
    sen5xstate.one_shot_mode = oneShotMode;

    if (model != SEN50) {
        sen5xstate.has_voc_state_time = true;
        sen5xstate.has_voc_state_valid = true;
        sen5xstate.has_voc_state_array = true;

        sen5xstate.voc_state_time = vocTime;
        sen5xstate.voc_state_valid = vocValid;
        // Unpack state (8 bytes)
        sen5xstate.voc_state_array = (((uint64_t) vocState[7]) << 56) |
            ((uint64_t) vocState[6] << 48) |
            ((uint64_t) vocState[5] << 40) |
            ((uint64_t) vocState[4] << 32) |
            ((uint64_t) vocState[3] << 24) |
            ((uint64_t) vocState[2] << 16) |
            ((uint64_t) vocState[1] << 8) |
            ((uint64_t) vocState[0]);
    }

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
    // uint32_t now;
    // now = getValidTime(RTCQuality::RTCQualityDevice);
    LOG_DEBUG("SEN5X: Waking up sensor");

    // NOTE - No need to send it everytime if we switch to RHT/gas only mode
    // // Check if state is recent, less than 10 minutes (600 seconds)
    // if (vocStateRecent(now) && vocStateValid()) {
    //     if (!vocStateToSensor()){
    //         LOG_ERROR("SEN5X: Sending VOC state to sensor failed");
    //     }
    // } else {
    //     LOG_DEBUG("SEN5X: No valid VOC state found. Ignoring");
    // }

    if (!sendCommand(SEN5X_START_MEASUREMENT)) {
        LOG_ERROR("SEN5X: Error starting measurement");
        // TODO - what should this return?? Something actually on the default interval
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    }
    delay(50); // From Sensirion Datasheet

    // TODO - This is currently "problematic"
    // If time is updated in between reads, there is no way to
    // keep track of how long it has passed
    pmMeasureStarted = getTime();
    state = SEN5X_MEASUREMENT;
    if (state == SEN5X_MEASUREMENT)
        LOG_INFO("SEN5X: Started measurement mode");
    return SEN5X_WARMUP_MS_1;
}

bool SEN5XSensor::vocStateStable()
{
    uint32_t now;
    now = getTime();
    uint32_t sinceFirstMeasureStarted = (now - rhtGasMeasureStarted);
    LOG_DEBUG("sinceFirstMeasureStarted: %us", sinceFirstMeasureStarted);
    return sinceFirstMeasureStarted > SEN5X_VOC_STATE_WARMUP_S;
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

bool SEN5XSensor::initDevice(TwoWire *bus, ScanI2C::FoundDevice *dev)
{
    state = SEN5X_NOT_DETECTED;
    LOG_INFO("Init sensor: %s", sensorName);

    _bus = bus;
    _address = dev->address.address;

    delay(50); // without this there is an error on the deviceReset function

    if (!sendCommand(SEN5X_RESET)) {
        LOG_ERROR("SEN5X: Error reseting device");
        return false;
    }
    delay(200); // From Sensirion Datasheet

    if (!findModel()) {
        LOG_ERROR("SEN5X: error finding sensor model");
        return false;
    }

    // Check the firmware version
    if (!getVersion()) return false;
    if (firmwareVer < 2) {
        LOG_ERROR("SEN5X: error firmware is too old and will not work with this implementation");
        return false;
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
            // We assume the device has just been updated or it is new,
            // so no need to trigger a cleaning.
            // Just save the timestamp to do a cleaning one week from now.
            // Otherwise, we will never trigger cleaning in some cases
            lastCleaning = now;
            lastCleaningValid = true;
            LOG_INFO("SEN5X: No valid last cleaning date found, saving it now: %us", lastCleaning);
            saveState();
        }

        if (model != SEN50) {
            if (!vocValid) {
                LOG_INFO("SEN5X: No valid VOC's state found");
            } else {
                // Check if state is recent
                if (vocStateRecent(now)) {
                    // If current date greater than 01/01/2018 (validity check)
                    // Send it to the sensor
                    LOG_INFO("SEN5X: VOC state is valid and recent");
                    vocStateToSensor();
                } else {
                    LOG_INFO("SEN5X: VOC state is too old or date is invalid");
                    LOG_DEBUG("SEN5X: vocTime %u, Passed %u, and now %u", vocTime, passed, now);
                }
            }
        }
    } else {
        // TODO - Should this actually ignore? We could end up never cleaning...
        LOG_INFO("SEN5X: Not enough RTCQuality, ignoring saved state. Trying again later");
    }

    idle(false);
    rhtGasMeasureStarted = now;

    initI2CSensor();
    return true;
}

bool SEN5XSensor::readValues()
{
    if (!sendCommand(SEN5X_READ_VALUES)){
        LOG_ERROR("SEN5X: Error sending read command");
        return false;
    }
    LOG_DEBUG("SEN5X: Reading PM Values");
    delay(20); // From Sensirion Datasheet

    uint8_t dataBuffer[16];
    size_t receivedNumber = readBuffer(&dataBuffer[0], 24);
    if (receivedNumber == 0) {
        LOG_ERROR("SEN5X: Error getting values");
        return false;
    }

    // Get the integers
    uint16_t uint_pM1p0        = static_cast<uint16_t>((dataBuffer[0]  << 8) | dataBuffer[1]);
    uint16_t uint_pM2p5        = static_cast<uint16_t>((dataBuffer[2]  << 8) | dataBuffer[3]);
    uint16_t uint_pM4p0        = static_cast<uint16_t>((dataBuffer[4]  << 8) | dataBuffer[5]);
    uint16_t uint_pM10p0       = static_cast<uint16_t>((dataBuffer[6]  << 8) | dataBuffer[7]);

    int16_t  int_humidity      = static_cast<int16_t>((dataBuffer[8]   << 8) | dataBuffer[9]);
    int16_t  int_temperature   = static_cast<int16_t>((dataBuffer[10]  << 8) | dataBuffer[11]);
    int16_t  int_vocIndex      = static_cast<int16_t>((dataBuffer[12]  << 8) | dataBuffer[13]);
    int16_t  int_noxIndex      = static_cast<int16_t>((dataBuffer[14]  << 8) | dataBuffer[15]);

    // Convert values based on Sensirion Arduino lib
    sen5xmeasurement.pM1p0          = !isnan(uint_pM1p0) ? uint_pM1p0 / 10 : UINT16_MAX;
    sen5xmeasurement.pM2p5          = !isnan(uint_pM2p5) ? uint_pM2p5 / 10 : UINT16_MAX;
    sen5xmeasurement.pM4p0          = !isnan(uint_pM4p0) ? uint_pM4p0 / 10 : UINT16_MAX;
    sen5xmeasurement.pM10p0         = !isnan(uint_pM10p0) ? uint_pM10p0 / 10 : UINT16_MAX;
    sen5xmeasurement.humidity       = !isnan(int_humidity) ? int_humidity / 100.0f : FLT_MAX;
    sen5xmeasurement.temperature    = !isnan(int_temperature) ? int_temperature / 200.0f : FLT_MAX;
    sen5xmeasurement.vocIndex       = !isnan(int_vocIndex) ? int_vocIndex / 10.0f : FLT_MAX;
    sen5xmeasurement.noxIndex       = !isnan(int_noxIndex) ? int_noxIndex / 10.0f : FLT_MAX;

    LOG_DEBUG("Got: pM1p0=%u, pM2p5=%u, pM4p0=%u, pM10p0=%u",
                sen5xmeasurement.pM1p0, sen5xmeasurement.pM2p5,
                sen5xmeasurement.pM4p0, sen5xmeasurement.pM10p0);

    if (model != SEN50) {
        LOG_DEBUG("Got: humidity=%.2f, temperature=%.2f, vocIndex=%.2f",
                    sen5xmeasurement.humidity, sen5xmeasurement.temperature,
                    sen5xmeasurement.vocIndex);
    }

    if (model == SEN55) {
        LOG_DEBUG("Got: noxIndex=%.2f",
            sen5xmeasurement.noxIndex);
    }

    return true;
}

bool SEN5XSensor::readPNValues(bool cumulative)
{
    if (!sendCommand(SEN5X_READ_PM_VALUES)){
        LOG_ERROR("SEN5X: Error sending read command");
        return false;
    }

    LOG_DEBUG("SEN5X: Reading PN Values");
    delay(20); // From Sensirion Datasheet

    uint8_t dataBuffer[20];
    size_t receivedNumber = readBuffer(&dataBuffer[0], 30);
    if (receivedNumber == 0) {
        LOG_ERROR("SEN5X: Error getting PN values");
        return false;
    }

    // Get the integers
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
    // Multiply by 100 for converting from #/cm3 to #/0.1l for PN values
    sen5xmeasurement.pN0p5   = !isnan(uint_pN0p5) ? uint_pN0p5  / 10 * 100 : UINT32_MAX;
    sen5xmeasurement.pN1p0   = !isnan(uint_pN1p0) ? uint_pN1p0  / 10 * 100 : UINT32_MAX;
    sen5xmeasurement.pN2p5   = !isnan(uint_pN2p5) ? uint_pN2p5  / 10 * 100 : UINT32_MAX;
    sen5xmeasurement.pN4p0   = !isnan(uint_pN4p0) ? uint_pN4p0  / 10 * 100 : UINT32_MAX;
    sen5xmeasurement.pN10p0  = !isnan(uint_pN10p0) ? uint_pN10p0 / 10 * 100 : UINT32_MAX;
    sen5xmeasurement.tSize   = !isnan(uint_tSize) ? uint_tSize  / 1000.0f : FLT_MAX;

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

//     uint8_t dataBuffer[8];
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

    if(!readPNValues(false)) {
        LOG_ERROR("SEN5X: Error getting PN readings");
        return 2;
    }

    // if(!readRawValues()) {
    //     LOG_ERROR("SEN5X: Error getting Raw readings");
    //     return 2;
    // }

    return 0;
}

int32_t SEN5XSensor::wakeUpTimeMs()
{
    return SEN5X_WARMUP_MS_2;
}

int32_t SEN5XSensor::pendingForReadyMs(){
    uint32_t now;
    now = getTime();
    uint32_t sincePmMeasureStarted = (now - pmMeasureStarted)*1000;
    LOG_DEBUG("SEN5X: Since measure started: %ums", sincePmMeasureStarted);

    switch (state) {
        case SEN5X_MEASUREMENT: {

            if (sincePmMeasureStarted < SEN5X_WARMUP_MS_1) {
                LOG_INFO("SEN5X: not enough time passed since starting measurement");
                return SEN5X_WARMUP_MS_1 - sincePmMeasureStarted;
            }

            if (!pmMeasureStarted) {
                pmMeasureStarted = now;
            }

            // Get PN values to check if we are above or below threshold
            readPNValues(true);

            // If the reading is low (the tyhreshold is in #/cm3) and second warmUp hasn't passed we return to come back later
            if ((sen5xmeasurement.pN4p0 / 100) < SEN5X_PN4P0_CONC_THD && sincePmMeasureStarted < SEN5X_WARMUP_MS_2) {
                LOG_INFO("SEN5X: Concentration is low, we will ask again in the second warm up period");
                state = SEN5X_MEASUREMENT_2;
                // Report how many seconds are pending to cover the first warm up period
                return SEN5X_WARMUP_MS_2 - sincePmMeasureStarted;
            }
            return 0;
        }
        case SEN5X_MEASUREMENT_2: {
            if (sincePmMeasureStarted < SEN5X_WARMUP_MS_2) {
                // Report how many seconds are pending to cover the first warm up period
                return SEN5X_WARMUP_MS_2 - sincePmMeasureStarted;
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
        if (sen5xmeasurement.pM1p0 != UINT16_MAX) {
            measurement->variant.air_quality_metrics.has_pm10_standard = true;
            measurement->variant.air_quality_metrics.pm10_standard = sen5xmeasurement.pM1p0;
        }
        if (sen5xmeasurement.pM2p5 != UINT16_MAX) {
            measurement->variant.air_quality_metrics.has_pm25_standard = true;
            measurement->variant.air_quality_metrics.pm25_standard = sen5xmeasurement.pM2p5;
        }
        if (sen5xmeasurement.pM4p0 != UINT16_MAX) {
            measurement->variant.air_quality_metrics.has_pm40_standard = true;
            measurement->variant.air_quality_metrics.pm40_standard = sen5xmeasurement.pM4p0;
        }
        if (sen5xmeasurement.pM10p0 != UINT16_MAX) {
            measurement->variant.air_quality_metrics.has_pm100_standard = true;
            measurement->variant.air_quality_metrics.pm100_standard = sen5xmeasurement.pM10p0;
        }
        if (sen5xmeasurement.pN0p5 != UINT32_MAX) {
            measurement->variant.air_quality_metrics.has_particles_05um = true;
            measurement->variant.air_quality_metrics.particles_05um = sen5xmeasurement.pN0p5;
        }
        if (sen5xmeasurement.pN1p0 != UINT32_MAX) {
            measurement->variant.air_quality_metrics.has_particles_10um = true;
            measurement->variant.air_quality_metrics.particles_10um = sen5xmeasurement.pN1p0;
        }
        if (sen5xmeasurement.pN2p5 != UINT32_MAX) {
            measurement->variant.air_quality_metrics.has_particles_25um = true;
            measurement->variant.air_quality_metrics.particles_25um = sen5xmeasurement.pN2p5;
        }
        if (sen5xmeasurement.pN4p0 != UINT32_MAX) {
            measurement->variant.air_quality_metrics.has_particles_40um = true;
            measurement->variant.air_quality_metrics.particles_40um = sen5xmeasurement.pN4p0;
        }
        if (sen5xmeasurement.pN10p0 != UINT32_MAX) {
            measurement->variant.air_quality_metrics.has_particles_100um = true;
            measurement->variant.air_quality_metrics.particles_100um = sen5xmeasurement.pN10p0;
        }
        if (sen5xmeasurement.tSize != FLT_MAX) {
            measurement->variant.air_quality_metrics.has_particles_tps = true;
            measurement->variant.air_quality_metrics.particles_tps = sen5xmeasurement.tSize;
        }

        if (model != SEN50) {
            if (sen5xmeasurement.humidity!= FLT_MAX) {
                measurement->variant.air_quality_metrics.has_pm_humidity = true;
                measurement->variant.air_quality_metrics.pm_humidity = sen5xmeasurement.humidity;
            }
            if (sen5xmeasurement.temperature!= FLT_MAX) {
                measurement->variant.air_quality_metrics.has_pm_temperature = true;
                measurement->variant.air_quality_metrics.pm_temperature = sen5xmeasurement.temperature;
            }
            if (sen5xmeasurement.noxIndex!= FLT_MAX) {
                measurement->variant.air_quality_metrics.has_pm_voc_idx = true;
                measurement->variant.air_quality_metrics.pm_voc_idx = sen5xmeasurement.vocIndex;
            }
        }

        if (model == SEN55) {
            if (sen5xmeasurement.noxIndex!= FLT_MAX) {
                measurement->variant.air_quality_metrics.has_pm_nox_idx = true;
                measurement->variant.air_quality_metrics.pm_nox_idx = sen5xmeasurement.noxIndex;
            }
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

void SEN5XSensor::setMode(bool setOneShot) {
    oneShotMode = setOneShot;
}

AdminMessageHandleResult SEN5XSensor::handleAdminMessage(const meshtastic_MeshPacket &mp, meshtastic_AdminMessage *request,
                                                           meshtastic_AdminMessage *response)
{
    AdminMessageHandleResult result;
    result = AdminMessageHandleResult::NOT_HANDLED;


    switch (request->which_payload_variant) {
        case meshtastic_AdminMessage_sensor_config_tag:
            if (!request->sensor_config.has_sen5x_config) {
                result = AdminMessageHandleResult::NOT_HANDLED;
                break;
            }

            // TODO - Add admin command to set temperature offset
            // Check for temperature offset
            // if (request->sensor_config.sen5x_config.has_set_temperature) {
            //     this->setTemperature(request->sensor_config.sen5x_config.set_temperature);
            // }

            // Check for one-shot/continuous mode request
            if (request->sensor_config.sen5x_config.has_set_one_shot_mode) {
                this->setMode(request->sensor_config.sen5x_config.set_one_shot_mode);
            }

            result = AdminMessageHandleResult::HANDLED;
            break;

        default:
            result = AdminMessageHandleResult::NOT_HANDLED;
    }

    return result;
}

#endif