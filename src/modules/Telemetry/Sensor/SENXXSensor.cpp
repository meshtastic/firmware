#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_AIR_QUALITY_SENSOR

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "FSCommon.h"
#include "SENXXSensor.h"
#include "SPILock.h"
#include "SafeFile.h"
#include "TelemetrySensor.h"
#include <float.h> // FLT_MAX
#include <pb_decode.h>
#include <pb_encode.h>
#include <string.h> // memcpy

bool SENXXSensor::getVersion()
{
    if (!sendCommand(SENXX_GET_FIRMWARE_VERSION)) {
        LOG_ERROR("%s: Error sending version command", sensorName);
        return false;
    }
    delay(20); // From Sensirion Datasheet

    // Version reply layout: fw major/minor, fw debug, hw major/minor,
    // protocol major/minor, padding
    uint8_t versionBuffer[SENXX_VERSION_BUFFER_SIZE]{};
    size_t charNumber = readBuffer(&versionBuffer[0], SENXX_VERSION_BUFFER_SIZE + (SENXX_VERSION_BUFFER_SIZE / 2));
    if (charNumber < SENXX_VERSION_BUFFER_SIZE) {
        LOG_ERROR("%s: Error getting device version value", sensorName);
        return false;
    }

    firmwareVer = versionBuffer[0] + (versionBuffer[1] / 10.0f);
    hardwareVer = versionBuffer[3] + (versionBuffer[4] / 10.0f);
    protocolVer = versionBuffer[5] + (versionBuffer[6] / 10.0f);

    LOG_INFO("%s: Firmware Version: %0.2f", sensorName, firmwareVer);
    LOG_INFO("%s: Hardware Version: %0.2f", sensorName, hardwareVer);
    LOG_INFO("%s: Protocol Version: %0.2f", sensorName, protocolVer);

    return true;
}

void SENXXSensor::updateCapabilities()
{
    hasRHT = hasVOC = hasNOx = hasCO2 = hasHCHO = false;
    readMeasuredValuesCmd = 0;

    switch (model) {
    case SEN50:
        break;
    case SEN54:
        hasRHT = true;
        hasVOC = true;
        break;
    case SEN55:
        hasRHT = true;
        hasVOC = true;
        hasNOx = true;
        break;
    case SEN62:
        hasRHT = true;
        readMeasuredValuesCmd = 0x04A3;
        break;
    case SEN63C:
        hasRHT = true;
        hasCO2 = true;
        readMeasuredValuesCmd = 0x0471;
        break;
    case SEN65:
        hasRHT = true;
        hasVOC = true;
        hasNOx = true;
        readMeasuredValuesCmd = 0x0446;
        break;
    case SEN66:
        hasRHT = true;
        hasVOC = true;
        hasNOx = true;
        hasCO2 = true;
        readMeasuredValuesCmd = 0x0300;
        break;
    case SEN68:
        hasRHT = true;
        hasVOC = true;
        hasNOx = true;
        hasHCHO = true;
        readMeasuredValuesCmd = 0x0467;
        break;
    case SEN69C:
        hasRHT = true;
        hasVOC = true;
        hasNOx = true;
        hasHCHO = true;
        hasCO2 = true;
        readMeasuredValuesCmd = 0x04B5;
        break;
    default:
        break;
    }
}

bool SENXXSensor::findModel()
{
    if (!sendCommand(SENXX_GET_PRODUCT_NAME)) {
        LOG_ERROR("%s: Error asking for product name", sensorName);
        return false;
    }
    delay(50); // From Sensirion Datasheet

    uint8_t name[SENXX_PRODUCT_NAME_BUFFER_SIZE]{};
    size_t charNumber = readBuffer(&name[0], SENXX_PRODUCT_NAME_BUFFER_SIZE + (SENXX_PRODUCT_NAME_BUFFER_SIZE / 2));

    if (charNumber < SENXX_PRODUCT_NAME_BUFFER_SIZE) {
        LOG_ERROR("%s: Error getting device name", sensorName);
        return false;
    }

    // Every model's product name follows "SEN<family digit><variant digit>[C]",
    // e.g. "SEN50", "SEN55", "SEN63C", "SEN69C" - so name[3] picks the family
    // (SEN5X vs SEN6X) and name[4] picks the exact variant within it.
    model = SENXX_UNKNOWN;
    if (name[3] == '5') {
        switch (name[4]) {
        case '0':
            model = SEN50;
            break;
        case '4':
            model = SEN54;
            break;
        case '5':
            model = SEN55;
            break;
        }
    } else if (name[3] == '6') {
        switch (name[4]) {
        case '2':
            model = SEN62;
            break;
        case '3':
            model = SEN63C;
            break;
        case '5':
            model = SEN65;
            break;
        case '6':
            model = SEN66;
            break;
        case '8':
            model = SEN68;
            break;
        case '9':
            model = SEN69C;
            break;
        }
    }

    if (model == SENXX_UNKNOWN) {
        return false;
    }

    updateCapabilities();
    LOG_INFO("%s: found sensor model %s", sensorName, (const char *)name);
    return true;
}

bool SENXXSensor::probe(TwoWire *bus, uint8_t address, ScanI2C::I2CPort port)
{
    LOG_INFO("%s: probing sensor", sensorName);

    _bus = bus;
    _address = address;

#ifdef SENXX_I2C_CLOCK_SPEED
    _port = port;
    reClockI2C.setup(_bus, _port);
    ReClockI2CGuard clockGuard(reClockI2C, SENXX_I2C_CLOCK_SPEED);
#endif /* SENXX_I2C_CLOCK_SPEED */

    if (!findModel()) {
        LOG_DEBUG("%s: can't find sensor model", sensorName);
        return false;
    }

    return true;
}

bool SENXXSensor::sendCommand(uint16_t command)
{
    uint8_t nothing;
    return sendCommand(command, &nothing, 0);
}

bool SENXXSensor::sendCommand(uint16_t command, uint8_t *buffer, uint8_t byteNumber)
{
    // At least we need two bytes for the command
    uint8_t bufferSize = 2;

    // Add space for CRC bytes (one every two bytes)
    if (byteNumber > 0)
        bufferSize += byteNumber + (byteNumber / 2);

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
            uint8_t calcCRC = senxxCRC(&buffer[bi - 2]);
            toSend[i++] = calcCRC;
        }
    }

    // Note: this delay is necessary to allow for long-buffers
    delay(20);
    _bus->beginTransmission(_address);
    size_t writtenBytes = _bus->write(toSend, bufferSize);
    uint8_t i2c_error = _bus->endTransmission();

    if (writtenBytes != bufferSize) {
        LOG_ERROR("%s: Error writing on I2C bus", sensorName);
        return false;
    }

    if (i2c_error != 0) {
        LOG_ERROR("%s: Error on I2C communication: %x", sensorName, i2c_error);
        return false;
    }
    return true;
}

uint8_t SENXXSensor::readBuffer(uint8_t *buffer, uint8_t byteNumber)
{
    size_t readBytes = _bus->requestFrom(_address, byteNumber);
    if (readBytes != byteNumber) {
        LOG_ERROR("%s: Error reading I2C bus", sensorName);
        return 0;
    }

    uint8_t i = 0;
    uint8_t receivedBytes = 0;
    while (readBytes > 0) {
        buffer[i++] = _bus->read(); // Just as a reminder: i++ returns i and after that increments.
        buffer[i++] = _bus->read();
        uint8_t recvCRC = _bus->read();
        uint8_t calcCRC = senxxCRC(&buffer[i - 2]);
        if (recvCRC != calcCRC) {
            LOG_ERROR("%s: Checksum error while receiving msg", sensorName);
            return 0;
        }
        readBytes -= 3;
        receivedBytes += 2;
    }

    return receivedBytes;
}

uint8_t SENXXSensor::senxxCRC(const uint8_t *buffer)
{
    // This code is based on Sensirion's own implementation
    // https://github.com/Sensirion/arduino-core/blob/41fd02cacf307ec4945955c58ae495e56809b96c/src/SensirionCrc.cpp
    // Identical CRC8 (poly 0x31, init 0xFF) is used by the whole SEN5X/SEN6X family.
    uint8_t crc = 0xff;

    for (uint8_t i = 0; i < 2; i++) {

        crc ^= buffer[i];

        for (uint8_t bit = 8; bit > 0; bit--) {
            if (crc & 0x80)
                crc = (crc << 1) ^ 0x31;
            else
                crc = (crc << 1);
        }
    }

    return crc;
}

void SENXXSensor::sleep()
{
    if (state == SENXX_CLEANING) {
        // The scheduler's periodic "put idle-able sensors to sleep" housekeeping can reach
        // here while a cleaning cycle is still running (isActive() reports SENXX_CLEANING as
        // active). Don't let it interrupt the cycle - pendingForReadyMs()/finishCleaning()
        // owns the transition out of SENXX_CLEANING.
        LOG_INFO("%s: Not going to sleep, fan cleaning is in progress", sensorName);
        return;
    }
#ifdef SENXX_I2C_CLOCK_SPEED
    ReClockI2CGuard clockGuard(reClockI2C, SENXX_I2C_CLOCK_SPEED);
#endif /* SENXX_I2C_CLOCK_SPEED */
    idle(true);
}

bool SENXXSensor::idle(bool checkState)
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
        // If the stabilisation period is not passed for a model with a VOC sensor, don't go to idle
        if (hasVOC) {
            // Get VOC state before going to idle mode
            vocValid = false;
            if (vocStateFromSensor()) {
                vocValid = vocStateValid();
                // Check if we have time, and store it
                uint32_t now; // If time is RTCQualityNone, it will return zero
                now = getValidTime(RTCQuality::RTCQualityDevice);
                // Check if state is valid (non-zero)
                if (now) {
                    vocTime = now;
                }
            }

            if (!(vocStateStable() && vocValid)) {
                LOG_INFO("%s: Not stopping measurement, vocState is not stable yet!", sensorName);
                return true;
            }
        }
        // Save state and prefs (on all models)
        saveState();
    }

    if (!oneShotMode) {
        LOG_INFO("%s: Not stopping measurement, continuous mode!", sensorName);
        return true;
    } else {
        LOG_INFO("%s: One shot mode enabled", sensorName);
    }

    // SEN6X has no low-power "RHT/Gas only" mode - it must always fully stop.
    // Within SEN5X, models without gas sensing (SEN50) also fully stop; SEN54/SEN55
    // instead switch to the RHT/Gas-only mode to keep the VOC engine warm.
    // TODO - Decide if for variants with VOC/NOx sensor, the device will be kept on to avoid messing
    // up with the engine. In principle, since we are giving the VOC state, the algorithm should work fine,
    // however, from tests, we don't see the same.
    // Recommendation: if it has VOC / NOx, suggest NOT to use oneShot mode
    if (isSen6xFamily() || !hasVOC) {
        if (!sendCommand(SENXX_STOP_MEASUREMENT)) {
            LOG_ERROR("%s: Error stopping measurement", sensorName);
            return false;
        }
        state = SENXX_IDLE;
        LOG_INFO("%s: Stop measurement mode", sensorName);
    } else {
        if (!sendCommand(SEN5X_START_MEASUREMENT_RHT_GAS)) {
            LOG_ERROR("%s: Error switching to RHT/Gas measurement", sensorName);
            return false;
        }
        state = SENXX_RHTGAS_ONLY;
        LOG_INFO("%s: Switch to RHT/Gas only measurement mode", sensorName);
    }

    delay(200); // From Sensirion Datasheet
    pmMeasureStarted = 0;
    return true;
}

bool SENXXSensor::vocStateRecent(uint32_t now)
{
    if (now) {
        uint32_t passed = now - vocTime; // in seconds

        // Check if state is recent, less than 10 minutes (600 seconds)
        if (passed < SENXX_VOC_VALID_TIME && (now > SENXX_VOC_VALID_DATE)) {
            return true;
        }
    }
    return false;
}

bool SENXXSensor::vocStateValid()
{
    if (!vocState[0] && !vocState[1] && !vocState[2] && !vocState[3] && !vocState[4] && !vocState[5] && !vocState[6] &&
        !vocState[7]) {
        LOG_DEBUG("%s: VOC state is all 0, invalid", sensorName);
        return false;
    } else {
        LOG_DEBUG("%s: VOC state is valid", sensorName);
        return true;
    }
}

bool SENXXSensor::vocStateToSensor()
{
    if (!hasVOC) {
        return true;
    }

    if (!vocStateValid()) {
        LOG_INFO("%s: VOC state is invalid, not sending", sensorName);
        return true;
    }

    if (!sendCommand(SENXX_STOP_MEASUREMENT)) {
        LOG_ERROR("%s: Error stopping measurement", sensorName);
        return false;
    }
    delay(200); // From Sensirion Datasheet

    LOG_DEBUG("%s: Sending VOC state to sensor", sensorName);
    LOG_DEBUG("[%u, %u, %u, %u, %u, %u, %u, %u]", vocState[0], vocState[1], vocState[2], vocState[3], vocState[4], vocState[5],
              vocState[6], vocState[7]);

    // Note: send command already takes into account the CRC
    // buffer size increment needed
    if (!sendCommand(SENXX_RW_VOCS_STATE, vocState, SENXX_VOC_STATE_BUFFER_SIZE)) {
        LOG_ERROR("%s: Error sending VOC's state command", sensorName);
        return false;
    }

    return true;
}

bool SENXXSensor::vocStateFromSensor()
{
    if (!hasVOC) {
        return true;
    }

    LOG_INFO("%s: Getting VOC state from sensor", sensorName);
    //  Ask VOCs state from the sensor
    if (!sendCommand(SENXX_RW_VOCS_STATE)) {
        LOG_ERROR("%s: Error sending VOC's state command", sensorName);
        return false;
    }

    delay(20); // From Sensirion Datasheet

    // Retrieve the data into a staging buffer so a partial read (e.g. a CRC
    // failure halfway through) cannot corrupt the current vocState.
    // The requested size accounts for the CRC bytes
    uint8_t stateBuffer[SENXX_VOC_STATE_BUFFER_SIZE]{};
    size_t receivedNumber = readBuffer(&stateBuffer[0], SENXX_VOC_STATE_BUFFER_SIZE + (SENXX_VOC_STATE_BUFFER_SIZE / 2));
    delay(20); // From Sensirion Datasheet

    if (receivedNumber < SENXX_VOC_STATE_BUFFER_SIZE) {
        LOG_DEBUG("%s: Error getting VOC's state", sensorName);
        return false;
    }
    memcpy(vocState, stateBuffer, SENXX_VOC_STATE_BUFFER_SIZE);

    // Print the state (if debug is on)
    LOG_DEBUG("%s: VOC state retrieved from sensor: [%u, %u, %u, %u, %u, %u, %u, %u]", sensorName, vocState[0], vocState[1],
              vocState[2], vocState[3], vocState[4], vocState[5], vocState[6], vocState[7]);

    return true;
}

bool SENXXSensor::loadState()
{
#ifdef FSCom
    spiLock->lock();
    auto file = FSCom.open(senXXStateFileName, FILE_O_READ);
    bool okay = false;
    if (file) {
        LOG_INFO("%s: state read from %s", sensorName, senXXStateFileName);

        bool decoded;
        uint32_t lastCleaningTime = 0;
        bool lastCleaningValidFlag = false;
        bool oneShot = true;
        uint32_t vocStateTime = 0;
        bool vocStateValidFlag = false;
        uint64_t vocStateArray = 0;

        if (isSen6xFamily()) {
            pb_istream_t stream = {&readcb, &file, meshtastic_SEN6XState_size};
            decoded = pb_decode(&stream, &meshtastic_SEN6XState_msg, &sen6xstate);
            if (decoded) {
                lastCleaningTime = sen6xstate.last_cleaning_time;
                lastCleaningValidFlag = sen6xstate.last_cleaning_valid;
                oneShot = sen6xstate.one_shot_mode;
                vocStateTime = sen6xstate.voc_state_time;
                vocStateValidFlag = sen6xstate.voc_state_valid;
                vocStateArray = sen6xstate.voc_state_array;
            } else {
                LOG_ERROR("%s: can't decode protobuf %s", sensorName, PB_GET_ERROR(&stream));
            }
        } else {
            pb_istream_t stream = {&readcb, &file, meshtastic_SEN5XState_size};
            decoded = pb_decode(&stream, &meshtastic_SEN5XState_msg, &sen5xstate);
            if (decoded) {
                lastCleaningTime = sen5xstate.last_cleaning_time;
                lastCleaningValidFlag = sen5xstate.last_cleaning_valid;
                oneShot = sen5xstate.one_shot_mode;
                vocStateTime = sen5xstate.voc_state_time;
                vocStateValidFlag = sen5xstate.voc_state_valid;
                vocStateArray = sen5xstate.voc_state_array;
            } else {
                LOG_ERROR("%s: can't decode protobuf %s", sensorName, PB_GET_ERROR(&stream));
            }
        }

        if (decoded) {
            lastCleaning = lastCleaningTime;
            lastCleaningValid = lastCleaningValidFlag;
            oneShotMode = oneShot;

            if (hasVOC) {
                vocTime = vocStateTime;
                vocValid = vocStateValidFlag;
                // Unpack state
                vocState[7] = (uint8_t)(vocStateArray >> 56);
                vocState[6] = (uint8_t)(vocStateArray >> 48);
                vocState[5] = (uint8_t)(vocStateArray >> 40);
                vocState[4] = (uint8_t)(vocStateArray >> 32);
                vocState[3] = (uint8_t)(vocStateArray >> 24);
                vocState[2] = (uint8_t)(vocStateArray >> 16);
                vocState[1] = (uint8_t)(vocStateArray >> 8);
                vocState[0] = (uint8_t)vocStateArray;
            }

            okay = true;
        }
        file.close();
    } else {
        LOG_INFO("%s: No state found (File: %s)", sensorName, senXXStateFileName);
    }
    spiLock->unlock();
    return okay;
#else
    LOG_ERROR("%s: Filesystem not implemented", sensorName);
    return false;
#endif
}

bool SENXXSensor::saveState()
{
#ifdef FSCom
    auto file = SafeFile(senXXStateFileName);

    // Pack VOC state (8 bytes)
    uint64_t vocStateArray = (((uint64_t)vocState[7]) << 56) | ((uint64_t)vocState[6] << 48) | ((uint64_t)vocState[5] << 40) |
                             ((uint64_t)vocState[4] << 32) | ((uint64_t)vocState[3] << 24) | ((uint64_t)vocState[2] << 16) |
                             ((uint64_t)vocState[1] << 8) | ((uint64_t)vocState[0]);

    bool encoded;
    LOG_INFO("%s: state write to %s", sensorName, senXXStateFileName);

    if (isSen6xFamily()) {
        sen6xstate.last_cleaning_time = lastCleaning;
        sen6xstate.last_cleaning_valid = lastCleaningValid;
        sen6xstate.one_shot_mode = oneShotMode;

        if (hasVOC) {
            sen6xstate.has_voc_state_time = true;
            sen6xstate.has_voc_state_valid = true;
            sen6xstate.has_voc_state_array = true;
            sen6xstate.voc_state_time = vocTime;
            sen6xstate.voc_state_valid = vocValid;
            sen6xstate.voc_state_array = vocStateArray;
        }

        pb_ostream_t stream = {&writecb, static_cast<Print *>(&file), meshtastic_SEN6XState_size};
        encoded = pb_encode(&stream, &meshtastic_SEN6XState_msg, &sen6xstate);
        if (!encoded)
            LOG_ERROR("%s: can't encode protobuf %s", sensorName, PB_GET_ERROR(&stream));
    } else {
        sen5xstate.last_cleaning_time = lastCleaning;
        sen5xstate.last_cleaning_valid = lastCleaningValid;
        sen5xstate.one_shot_mode = oneShotMode;

        if (hasVOC) {
            sen5xstate.has_voc_state_time = true;
            sen5xstate.has_voc_state_valid = true;
            sen5xstate.has_voc_state_array = true;
            sen5xstate.voc_state_time = vocTime;
            sen5xstate.voc_state_valid = vocValid;
            sen5xstate.voc_state_array = vocStateArray;
        }

        pb_ostream_t stream = {&writecb, static_cast<Print *>(&file), meshtastic_SEN5XState_size};
        encoded = pb_encode(&stream, &meshtastic_SEN5XState_msg, &sen5xstate);
        if (!encoded)
            LOG_ERROR("%s: can't encode protobuf %s", sensorName, PB_GET_ERROR(&stream));
    }

    bool okay = encoded;
    okay &= file.close();

    if (okay)
        LOG_INFO("%s: state write to %s successful", sensorName, senXXStateFileName);

    return okay;
#else
    LOG_ERROR("%s: Filesystem not implemented", sensorName);
    return false;
#endif
}

bool SENXXSensor::isActive()
{
    // SENXX_CLEANING counts as active so the scheduler polls pendingForReadyMs()
    // (which drives the cleaning cycle to completion) instead of calling wakeUp() again.
    return state == SENXX_MEASUREMENT || state == SENXX_MEASUREMENT_2 || state == SENXX_CLEANING;
}

bool SENXXSensor::checkRTCQualityImproved()
{
    RTCQuality currentQuality = getRTCQuality();
    if (currentQuality == lastRTCQuality) {
        return false;
    }
    LOG_DEBUG("%s: RTC quality changed: %s -> %s", sensorName, RtcName(lastRTCQuality), RtcName(currentQuality));
    bool gainedUsableClock = lastRTCQuality < RTCQuality::RTCQualityDevice && currentQuality >= RTCQuality::RTCQualityDevice;
    lastRTCQuality = currentQuality;
    return gainedUsableClock;
}

void SENXXSensor::reconcileTimeDependentState(uint32_t now)
{
    if (lastCleaningValid) {
        int32_t passed = now - lastCleaning; // in seconds

        if (passed > ONE_WEEK_IN_SECONDS && (now > SENXX_VOC_VALID_DATE)) {
            // If current date greater than 01/01/2018 (validity check)
            LOG_INFO("%s: More than a week (%us) since last cleaning in epoch (%us). Trigger, cleaning...", sensorName, passed,
                     lastCleaning);
            startCleaning();
        } else {
            LOG_INFO("%s: Cleaning not needed (%ds passed). Last cleaning date (in epoch): %us", sensorName, passed,
                     lastCleaning);
        }
    } else {
        // We assume the device has just been updated or it is new,
        // so no need to trigger a cleaning.
        // Just save the timestamp to do a cleaning one week from now.
        // Otherwise, we will never trigger cleaning in some cases
        lastCleaning = now;
        lastCleaningValid = true;
        LOG_INFO("%s: No valid last cleaning date found, saving it now: %us", sensorName, lastCleaning);
        saveState();
    }

    if (hasVOC) {
        if (!vocValid) {
            LOG_INFO("%s: No valid VOC's state found", sensorName);
        } else {
            // Check if state is recent
            if (vocStateRecent(now)) {
                // If current date greater than 01/01/2018 (validity check)
                // Send it to the sensor
                LOG_INFO("%s: VOC state is valid and recent", sensorName);
                vocStateToSensor();
            } else {
                LOG_INFO("%s: VOC state is too old or date is invalid", sensorName);
                LOG_DEBUG("%s: vocTime %u, and now %u", sensorName, vocTime, now);
            }
        }
    }
}

uint32_t SENXXSensor::wakeUp()
{
#ifdef SENXX_I2C_CLOCK_SPEED
    ReClockI2CGuard clockGuard(reClockI2C, SENXX_I2C_CLOCK_SPEED);
#endif /* SENXX_I2C_CLOCK_SPEED */
    return wakeUpInternal();
}

uint32_t SENXXSensor::wakeUpInternal()
{

    LOG_DEBUG("%s: Waking up sensor", sensorName);

    // The RTC may not have had a valid time when we last checked (e.g. right after boot,
    // before a WiFi/GPS/phone time source connected). Each wake is a natural, frequent point
    // to notice that it has since become valid and reconcile the saved cleaning/VOC state
    // against real elapsed time, instead of only ever checking once in initDevice().
    if (checkRTCQualityImproved()) {
        uint32_t now = getValidTime(RTCQuality::RTCQualityDevice);
        if (now) {
            LOG_INFO("%s: RTC became available (%s), reconciling saved cleaning/VOC state", sensorName, RtcName(lastRTCQuality));
            reconcileTimeDependentState(now);
            if (state == SENXX_CLEANING) {
                // A cleaning cycle was just started; let it run its course via
                // pendingForReadyMs() instead of overwriting state with the
                // measurement-start logic below.
                return SENXX_CLEANING_DURATION_MS;
            }
        }
    }

    if (!sendCommand(SENXX_START_MEASUREMENT)) {
        LOG_ERROR("%s: Error starting measurement", sensorName);
        // TODO - what should this return?? Something actually on the default interval?
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    }
    delay(50); // From Sensirion Datasheet

    pmMeasureStarted = millis();
    state = SENXX_MEASUREMENT;
    LOG_INFO("%s: Started measurement mode", sensorName);
    return SENXX_PM_WARMUP_MS_1;
}

bool SENXXSensor::vocStateStable()
{
    uint32_t sinceFirstMeasureStarted = (millis() - rhtGasMeasureStarted) / 1000;
    LOG_DEBUG("%s: sinceFirstMeasureStarted: %us", sensorName, sinceFirstMeasureStarted);
    return sinceFirstMeasureStarted > SENXX_VOC_STATE_WARMUP_S;
}

bool SENXXSensor::startCleaning()
{
    // Note: we only should enter here if we have a valid RTC with at least
    // RTCQuality::RTCQualityDevice
    SENXXState previousState = state;
    state = SENXX_CLEANING;

    // Note that cleaning command can only be run when the sensor is in measurement mode
    if (!sendCommand(SENXX_START_MEASUREMENT)) {
        LOG_ERROR("%s: Error starting measurement mode", sensorName);
        state = previousState;
        return false;
    }
    delay(50); // From Sensirion Datasheet

    if (!sendCommand(SENXX_START_FAN_CLEANING)) {
        LOG_ERROR("%s: Error starting fan cleaning", sensorName);
        state = previousState;
        return false;
    }
    delay(20); // From Sensirion Datasheet

    // This message will be always printed so the user knows the device it's not hung
    LOG_INFO("%s: Started fan cleaning it will take 10 seconds...", sensorName);

    // Don't block the caller for the ~10.5s the cycle takes - pendingForReadyMs()
    // polls SENXX_CLEANING and calls finishCleaning() once it's done.
    cleaningStarted = millis();
    return true;
}

void SENXXSensor::finishCleaning()
{
    LOG_INFO("%s: Cleaning done", sensorName);

    // Save timestamp in flash so we know when a week has passed
    uint32_t now;
    now = getValidTime(RTCQuality::RTCQualityDevice);
    if (now) {
        lastCleaning = now;
        lastCleaningValid = true;
        saveState();
    }

    idle();
}

bool SENXXSensor::initDevice(TwoWire *bus, ScanI2C::FoundDevice *dev)
{
    state = SENXX_NOT_DETECTED;
    LOG_INFO("%s: Init sensor", sensorName);

    _bus = bus;
    _address = dev->address.address;
#ifdef SENXX_I2C_CLOCK_SPEED
    _port = dev->address.port;
    reClockI2C.setup(_bus, _port);
    ReClockI2CGuard clockGuard(reClockI2C, SENXX_I2C_CLOCK_SPEED);
#endif /* SENXX_I2C_CLOCK_SPEED */

    delay(50); // without this there is an error on the deviceReset function

    if (!sendCommand(SENXX_RESET)) {
        LOG_ERROR("%s: error resetting device", sensorName);
        return false;
    }
    delay(200); // From Sensirion Datasheet

    if (!findModel()) {
        LOG_ERROR("%s: error finding sensor model", sensorName);
        return false;
    }

    // Check the firmware version
    if (!getVersion())
        return false;
    if (firmwareVer < 2) {
        LOG_ERROR("%s: firmware is too old and will not work with this implementation", sensorName);
        return false;
    }
    delay(200); // From Sensirion Datasheet

    // Detection succeeded
    state = SENXX_IDLE;
    status = 1;

    // Load state
    loadState();

    // Check if it is time to do a cleaning / whether the saved VOC state is still usable.
    // This needs a real clock; if we don't have one yet (typical right after boot, before
    // any time source has connected), don't lose the saved state - just defer the check.
    // wakeUp() re-checks getRTCQuality() on every wake via checkRTCQualityImproved() and
    // will run this same reconciliation the moment a valid time becomes available.
    lastRTCQuality = getRTCQuality();
    uint32_t now = getValidTime(RTCQuality::RTCQualityDevice);
    if (now) {
        reconcileTimeDependentState(now);
    } else {
        LOG_INFO("%s: Not enough RTCQuality yet, deferring saved cleaning/VOC state check until it improves", sensorName);
    }

    // If reconcileTimeDependentState() just started a cleaning cycle, leave state as
    // SENXX_CLEANING - idle(false) would send SENXX_STOP_MEASUREMENT and clobber it
    // mid-cycle. pendingForReadyMs() will poll it to completion once the scheduler starts.
    rhtGasMeasureStarted = millis();
    if (state != SENXX_CLEANING) {
        idle(false);
    }

    initI2CSensor();
    return true;
}

bool SENXXSensor::readValues()
{
    if (isSen6xFamily()) {
        if (!sendCommand(readMeasuredValuesCmd)) {
            LOG_ERROR("%s: Error sending read command", sensorName);
            return false;
        }
        LOG_DEBUG("%s: Reading measured values", sensorName);
        delay(20); // From Sensirion Datasheet

        // Fixed field order per the SEN6x datasheet: PM1.0, PM2.5, PM4.0, PM10.0,
        // [Humidity, Temperature], [VOC], [NOx], [HCHO], [CO2] - each block only
        // present if the model supports it.
        uint8_t wordCount = 4 + (hasRHT ? 2 : 0) + (hasVOC ? 1 : 0) + (hasNOx ? 1 : 0) + (hasHCHO ? 1 : 0) + (hasCO2 ? 1 : 0);
        uint8_t dataBuffer[20]{};
        size_t receivedNumber = readBuffer(&dataBuffer[0], wordCount * 3);
        if (receivedNumber < (size_t)(wordCount * 2)) {
            LOG_ERROR("%s: Error getting values", sensorName);
            return false;
        }

        uint8_t idx = 0;
        auto nextWord = [&dataBuffer, &idx]() -> int16_t {
            int16_t v = static_cast<int16_t>((dataBuffer[idx] << 8) | dataBuffer[idx + 1]);
            idx += 2;
            return v;
        };

        uint16_t uint_pM1p0 = static_cast<uint16_t>(nextWord());
        uint16_t uint_pM2p5 = static_cast<uint16_t>(nextWord());
        uint16_t uint_pM4p0 = static_cast<uint16_t>(nextWord());
        uint16_t uint_pM10p0 = static_cast<uint16_t>(nextWord());

        // Map values the sensor reports as unavailable (SENXX_UINT_INVALID /
        // SENXX_INT_INVALID) to the sentinels getMetrics() checks for
        senxxmeasurement.pM1p0 = (uint_pM1p0 != SENXX_UINT_INVALID) ? (uint_pM1p0 / 10) : UINT16_MAX;
        senxxmeasurement.pM2p5 = (uint_pM2p5 != SENXX_UINT_INVALID) ? (uint_pM2p5 / 10) : UINT16_MAX;
        senxxmeasurement.pM4p0 = (uint_pM4p0 != SENXX_UINT_INVALID) ? (uint_pM4p0 / 10) : UINT16_MAX;
        senxxmeasurement.pM10p0 = (uint_pM10p0 != SENXX_UINT_INVALID) ? (uint_pM10p0 / 10) : UINT16_MAX;

        senxxmeasurement.humidity = FLT_MAX;
        senxxmeasurement.temperature = FLT_MAX;
        senxxmeasurement.vocIndex = FLT_MAX;
        senxxmeasurement.noxIndex = FLT_MAX;
        senxxmeasurement.hcho = FLT_MAX;
        senxxmeasurement.co2 = FLT_MAX;

        LOG_DEBUG("%s: Got readings: pM1p0=%u, pM2p5=%u, pM4p0=%u, pM10p0=%u", sensorName, senxxmeasurement.pM1p0,
                  senxxmeasurement.pM2p5, senxxmeasurement.pM4p0, senxxmeasurement.pM10p0);

        if (hasRHT) {
            int16_t int_humidity = nextWord();
            int16_t int_temperature = nextWord();
            senxxmeasurement.humidity = (int_humidity != SENXX_INT_INVALID) ? (int_humidity / 100.0f) : FLT_MAX;
            senxxmeasurement.temperature = (int_temperature != SENXX_INT_INVALID) ? (int_temperature / 200.0f) : FLT_MAX;
            LOG_DEBUG("%s: Got readings: humidity=%.2f, temperature=%.2f", sensorName, senxxmeasurement.humidity,
                      senxxmeasurement.temperature);
        }
        if (hasVOC) {
            int16_t int_vocIndex = nextWord();
            senxxmeasurement.vocIndex = (int_vocIndex != SENXX_INT_INVALID) ? (int_vocIndex / 10.0f) : FLT_MAX;
            LOG_DEBUG("%s: Got readings: vocIndex=%.2f", sensorName, senxxmeasurement.vocIndex);
        }
        if (hasNOx) {
            int16_t int_noxIndex = nextWord();
            senxxmeasurement.noxIndex = (int_noxIndex != SENXX_INT_INVALID) ? (int_noxIndex / 10.0f) : FLT_MAX;
            LOG_DEBUG("%s: Got readings: noxIndex=%.2f", sensorName, senxxmeasurement.noxIndex);
        }
        if (hasHCHO) {
            uint16_t uint_hcho = static_cast<uint16_t>(nextWord());
            senxxmeasurement.hcho = (uint_hcho != SENXX_UINT_INVALID) ? (uint_hcho / 10.0f) : FLT_MAX;
            LOG_DEBUG("%s: Got readings: HCHO=%.2f", sensorName, senxxmeasurement.hcho);
        }
        if (hasCO2) {
            uint16_t uint_co2 = static_cast<uint16_t>(nextWord());
            senxxmeasurement.co2 = (uint_co2 != SENXX_UINT_INVALID) ? uint_co2 : FLT_MAX;
            LOG_DEBUG("%s: Got readings: CO2=%.2f", sensorName, senxxmeasurement.co2);
        }

        return true;
    }

    // SEN5X always answers with the same fixed 8-word layout (PM1/2.5/4/10, humidity,
    // temperature, VOC, NOx) regardless of model; unsupported fields simply come
    // back as Sensirion's "value unknown" placeholders.
    if (!sendCommand(SEN5X_READ_VALUES)) {
        LOG_ERROR("%s: Error sending read command", sensorName);
        return false;
    }
    LOG_DEBUG("%s: Reading PM Values", sensorName);
    delay(20); // From Sensirion Datasheet

    uint8_t dataBuffer[SEN5X_READ_VALUES_BUFFER_SIZE]{};
    size_t receivedNumber = readBuffer(&dataBuffer[0], SEN5X_READ_VALUES_BUFFER_SIZE + (SEN5X_READ_VALUES_BUFFER_SIZE / 2));
    if (receivedNumber < SEN5X_READ_VALUES_BUFFER_SIZE) {
        LOG_ERROR("%s: Error getting values", sensorName);
        return false;
    }

    // Get the integers
    uint16_t uint_pM1p0 = static_cast<uint16_t>((dataBuffer[0] << 8) | dataBuffer[1]);
    uint16_t uint_pM2p5 = static_cast<uint16_t>((dataBuffer[2] << 8) | dataBuffer[3]);
    uint16_t uint_pM4p0 = static_cast<uint16_t>((dataBuffer[4] << 8) | dataBuffer[5]);
    uint16_t uint_pM10p0 = static_cast<uint16_t>((dataBuffer[6] << 8) | dataBuffer[7]);

    int16_t int_humidity = static_cast<int16_t>((dataBuffer[8] << 8) | dataBuffer[9]);
    int16_t int_temperature = static_cast<int16_t>((dataBuffer[10] << 8) | dataBuffer[11]);
    int16_t int_vocIndex = static_cast<int16_t>((dataBuffer[12] << 8) | dataBuffer[13]);
    int16_t int_noxIndex = static_cast<int16_t>((dataBuffer[14] << 8) | dataBuffer[15]);

    // Convert values based on Sensirion Arduino lib. Map values the sensor
    // reports as unavailable (SENXX_UINT_INVALID / SENXX_INT_INVALID) to the
    // sentinels getMetrics() checks for
    senxxmeasurement.pM1p0 = (uint_pM1p0 != SENXX_UINT_INVALID) ? (uint_pM1p0 / 10) : UINT16_MAX;
    senxxmeasurement.pM2p5 = (uint_pM2p5 != SENXX_UINT_INVALID) ? (uint_pM2p5 / 10) : UINT16_MAX;
    senxxmeasurement.pM4p0 = (uint_pM4p0 != SENXX_UINT_INVALID) ? (uint_pM4p0 / 10) : UINT16_MAX;
    senxxmeasurement.pM10p0 = (uint_pM10p0 != SENXX_UINT_INVALID) ? (uint_pM10p0 / 10) : UINT16_MAX;
    senxxmeasurement.humidity = (int_humidity != SENXX_INT_INVALID) ? (int_humidity / 100.0f) : FLT_MAX;
    senxxmeasurement.temperature = (int_temperature != SENXX_INT_INVALID) ? (int_temperature / 200.0f) : FLT_MAX;
    senxxmeasurement.vocIndex = (int_vocIndex != SENXX_INT_INVALID) ? (int_vocIndex / 10.0f) : FLT_MAX;
    senxxmeasurement.noxIndex = (int_noxIndex != SENXX_INT_INVALID) ? (int_noxIndex / 10.0f) : FLT_MAX;
    senxxmeasurement.co2 = FLT_MAX;
    senxxmeasurement.hcho = FLT_MAX;

    LOG_DEBUG("%s: Got readings: pM1p0=%u, pM2p5=%u, pM4p0=%u, pM10p0=%u", sensorName, senxxmeasurement.pM1p0,
              senxxmeasurement.pM2p5, senxxmeasurement.pM4p0, senxxmeasurement.pM10p0);

    if (hasRHT) {
        LOG_DEBUG("%s: Got readings: humidity=%.2f, temperature=%.2f, vocIndex=%.2f", sensorName, senxxmeasurement.humidity,
                  senxxmeasurement.temperature, senxxmeasurement.vocIndex);
    }

    if (hasNOx) {
        LOG_DEBUG("%s: Got readings: noxIndex=%.2f", sensorName, senxxmeasurement.noxIndex);
    }

    return true;
}

bool SENXXSensor::readPNValues(bool cumulative)
{
    if (isSen6xFamily()) {
        if (!sendCommand(SEN6X_READ_NUMBER_CONCENTRATION_VALUES)) {
            LOG_ERROR("%s: Error sending read command", sensorName);
            return false;
        }

        LOG_DEBUG("%s: Reading PN Values", sensorName);
        delay(20); // From Sensirion Datasheet

        uint8_t dataBuffer[10]{};
        size_t receivedNumber = readBuffer(&dataBuffer[0], 15);
        if (receivedNumber < 10) {
            LOG_ERROR("%s: Error getting PN values", sensorName);
            return false;
        }

        uint16_t uint_pN0p5 = static_cast<uint16_t>((dataBuffer[0] << 8) | dataBuffer[1]);
        uint16_t uint_pN1p0 = static_cast<uint16_t>((dataBuffer[2] << 8) | dataBuffer[3]);
        uint16_t uint_pN2p5 = static_cast<uint16_t>((dataBuffer[4] << 8) | dataBuffer[5]);
        uint16_t uint_pN4p0 = static_cast<uint16_t>((dataBuffer[6] << 8) | dataBuffer[7]);
        uint16_t uint_pN10p0 = static_cast<uint16_t>((dataBuffer[8] << 8) | dataBuffer[9]);

        // Raw PN values are #/cm3 with 0.1 resolution; multiplying by 10 converts
        // to #/0.1l without the truncation of dividing first. Map values the
        // sensor reports as unavailable (SENXX_UINT_INVALID) to the sentinel.
        senxxmeasurement.pN0p5 = (uint_pN0p5 != SENXX_UINT_INVALID) ? ((uint32_t)uint_pN0p5 * 10) : UINT32_MAX;
        senxxmeasurement.pN1p0 = (uint_pN1p0 != SENXX_UINT_INVALID) ? ((uint32_t)uint_pN1p0 * 10) : UINT32_MAX;
        senxxmeasurement.pN2p5 = (uint_pN2p5 != SENXX_UINT_INVALID) ? ((uint32_t)uint_pN2p5 * 10) : UINT32_MAX;
        senxxmeasurement.pN4p0 = (uint_pN4p0 != SENXX_UINT_INVALID) ? ((uint32_t)uint_pN4p0 * 10) : UINT32_MAX;
        senxxmeasurement.pN10p0 = (uint_pN10p0 != SENXX_UINT_INVALID) ? ((uint32_t)uint_pN10p0 * 10) : UINT32_MAX;
        // Unlike SEN5X's number-concentration command, SEN6X's doesn't return a
        // "typical particle size" word.
        senxxmeasurement.tSize = FLT_MAX;

        // Remove accumulative values:
        // https://github.com/fablabbcn/smartcitizen-kit-2x/issues/85
        if (!cumulative) {
            if (senxxmeasurement.pN10p0 != UINT32_MAX && senxxmeasurement.pN4p0 != UINT32_MAX)
                senxxmeasurement.pN10p0 -= senxxmeasurement.pN4p0;
            if (senxxmeasurement.pN4p0 != UINT32_MAX && senxxmeasurement.pN2p5 != UINT32_MAX)
                senxxmeasurement.pN4p0 -= senxxmeasurement.pN2p5;
            if (senxxmeasurement.pN2p5 != UINT32_MAX && senxxmeasurement.pN1p0 != UINT32_MAX)
                senxxmeasurement.pN2p5 -= senxxmeasurement.pN1p0;
            if (senxxmeasurement.pN1p0 != UINT32_MAX && senxxmeasurement.pN0p5 != UINT32_MAX)
                senxxmeasurement.pN1p0 -= senxxmeasurement.pN0p5;
        }

        LOG_DEBUG("%s: Got readings: pN0p5=%u, pN1p0=%u, pN2p5=%u, pN4p0=%u, pN10p0=%u", sensorName, senxxmeasurement.pN0p5,
                  senxxmeasurement.pN1p0, senxxmeasurement.pN2p5, senxxmeasurement.pN4p0, senxxmeasurement.pN10p0);

        return true;
    }

    if (!sendCommand(SEN5X_READ_PM_VALUES)) {
        LOG_ERROR("%s: Error sending read command", sensorName);
        return false;
    }

    LOG_DEBUG("%s: Reading PN Values", sensorName);
    delay(20); // From Sensirion Datasheet

    uint8_t dataBuffer[SEN5X_READ_PM_BUFFER_SIZE]{};
    size_t receivedNumber = readBuffer(&dataBuffer[0], SEN5X_READ_PM_BUFFER_SIZE + (SEN5X_READ_PM_BUFFER_SIZE / 2));
    if (receivedNumber < SEN5X_READ_PM_BUFFER_SIZE) {
        LOG_ERROR("%s: Error getting PN values", sensorName);
        return false;
    }

    // Get the integers
    uint16_t uint_pN0p5 = static_cast<uint16_t>((dataBuffer[8] << 8) | dataBuffer[9]);
    uint16_t uint_pN1p0 = static_cast<uint16_t>((dataBuffer[10] << 8) | dataBuffer[11]);
    uint16_t uint_pN2p5 = static_cast<uint16_t>((dataBuffer[12] << 8) | dataBuffer[13]);
    uint16_t uint_pN4p0 = static_cast<uint16_t>((dataBuffer[14] << 8) | dataBuffer[15]);
    uint16_t uint_pN10p0 = static_cast<uint16_t>((dataBuffer[16] << 8) | dataBuffer[17]);
    uint16_t uint_tSize = static_cast<uint16_t>((dataBuffer[18] << 8) | dataBuffer[19]);

    // Convert values based on Sensirion Arduino lib. Raw PN values are #/cm3
    // with 0.1 resolution; multiplying by 10 converts to #/0.1l without the
    // truncation of dividing first. Map values the sensor reports as
    // unavailable (SENXX_UINT_INVALID) to the sentinel getMetrics() checks for.
    senxxmeasurement.pN0p5 = (uint_pN0p5 != SENXX_UINT_INVALID) ? ((uint32_t)uint_pN0p5 * 10) : UINT32_MAX;
    senxxmeasurement.pN1p0 = (uint_pN1p0 != SENXX_UINT_INVALID) ? ((uint32_t)uint_pN1p0 * 10) : UINT32_MAX;
    senxxmeasurement.pN2p5 = (uint_pN2p5 != SENXX_UINT_INVALID) ? ((uint32_t)uint_pN2p5 * 10) : UINT32_MAX;
    senxxmeasurement.pN4p0 = (uint_pN4p0 != SENXX_UINT_INVALID) ? ((uint32_t)uint_pN4p0 * 10) : UINT32_MAX;
    senxxmeasurement.pN10p0 = (uint_pN10p0 != SENXX_UINT_INVALID) ? ((uint32_t)uint_pN10p0 * 10) : UINT32_MAX;
    senxxmeasurement.tSize = (uint_tSize != SENXX_UINT_INVALID) ? (uint_tSize / 1000.0f) : FLT_MAX;

    // Remove accumuluative values:
    // https://github.com/fablabbcn/smartcitizen-kit-2x/issues/85
    if (!cumulative) {
        if (senxxmeasurement.pN10p0 != UINT32_MAX && senxxmeasurement.pN4p0 != UINT32_MAX)
            senxxmeasurement.pN10p0 -= senxxmeasurement.pN4p0;
        if (senxxmeasurement.pN4p0 != UINT32_MAX && senxxmeasurement.pN2p5 != UINT32_MAX)
            senxxmeasurement.pN4p0 -= senxxmeasurement.pN2p5;
        if (senxxmeasurement.pN2p5 != UINT32_MAX && senxxmeasurement.pN1p0 != UINT32_MAX)
            senxxmeasurement.pN2p5 -= senxxmeasurement.pN1p0;
        if (senxxmeasurement.pN1p0 != UINT32_MAX && senxxmeasurement.pN0p5 != UINT32_MAX)
            senxxmeasurement.pN1p0 -= senxxmeasurement.pN0p5;
    }

    LOG_DEBUG("%s: Got readings: pN0p5=%u, pN1p0=%u, pN2p5=%u, pN4p0=%u, pN10p0=%u, tSize=%.2f", sensorName,
              senxxmeasurement.pN0p5, senxxmeasurement.pN1p0, senxxmeasurement.pN2p5, senxxmeasurement.pN4p0,
              senxxmeasurement.pN10p0, senxxmeasurement.tSize);

    return true;
}

uint8_t SENXXSensor::getMeasurements()
{
    uint32_t now = millis();

    // Try to get new data
    if (!sendCommand(SENXX_READ_DATA_READY)) {
        LOG_ERROR("%s: Error sending command data ready flag", sensorName);
        return 2;
    }
    delay(20); // From Sensirion Datasheet

    uint8_t dataReadyBuffer[SENXX_DATA_READY_BUFFER_SIZE]{};
    size_t charNumber = readBuffer(&dataReadyBuffer[0], SENXX_DATA_READY_BUFFER_SIZE + (SENXX_DATA_READY_BUFFER_SIZE / 2));
    if (charNumber < SENXX_DATA_READY_BUFFER_SIZE) {
        LOG_ERROR("%s: Error getting device version value", sensorName);
        return 2;
    }

    bool dataReady = dataReadyBuffer[1];
    uint32_t sinceLastDataPollMs = now - lastDataPoll;
    // Check if data is ready, and if since last time we requested is less than SENXX_POLL_INTERVAL
    if (!dataReady || (sinceLastDataPollMs < SENXX_POLL_INTERVAL)) {
        LOG_INFO("%s: Data is not ready", sensorName);
        return 1;
    }

    if (!readValues()) {
        LOG_ERROR("%s: Error getting readings", sensorName);
        return 2;
    }

    if (!readPNValues(false)) {
        LOG_ERROR("%s: Error getting PN readings", sensorName);
        return 2;
    }

    lastDataPoll = now;

    return 0;
}

int32_t SENXXSensor::wakeUpTimeMs()
{
    return SENXX_PM_WARMUP_MS_2;
}

int32_t SENXXSensor::pendingForReadyMs()
{
#ifdef SENXX_I2C_CLOCK_SPEED
    // Only the SENXX_MEASUREMENT/SENXX_CLEANING branches below touch I2C, but this is only
    // ever called while isActive() (i.e. one of those, or SENXX_MEASUREMENT_2, which doesn't),
    // so bracketing unconditionally here is simpler than guarding each branch separately.
    ReClockI2CGuard clockGuard(reClockI2C, SENXX_I2C_CLOCK_SPEED);
#endif /* SENXX_I2C_CLOCK_SPEED */
    uint32_t now = millis();
    uint32_t sincePmMeasureStarted = now - pmMeasureStarted;
    LOG_DEBUG("%s: Since measure started: %ums", sensorName, sincePmMeasureStarted);

    switch (state) {
    case SENXX_MEASUREMENT: {

        if (!pmMeasureStarted) {
            pmMeasureStarted = now;
        }

        if (sincePmMeasureStarted < SENXX_PM_WARMUP_MS_1) {
            LOG_INFO("%s: not enough time passed since starting measurement", sensorName);
            return SENXX_PM_WARMUP_MS_1 - sincePmMeasureStarted;
        }

        // Get PN values to check if we are above or below threshold
        readPNValues(true);
        lastDataPoll = now;

        // If the reading is low (the threshold is in #/cm3) and second warmUp hasn't passed we return to come back later
        if ((senxxmeasurement.pN4p0 / 100) < SENXX_PN4P0_CONC_THD && sincePmMeasureStarted < SENXX_PM_WARMUP_MS_2) {
            LOG_INFO("%s: Concentration is low, we will ask again in the second warm up period", sensorName);
            state = SENXX_MEASUREMENT_2;
            // Report how many seconds are pending to cover the first warm up period
            return SENXX_PM_WARMUP_MS_2 - sincePmMeasureStarted;
        }
        // CO2 sensor has an additional warmup time
        if (hasCO2 && sincePmMeasureStarted < SEN6X_CO2_WARMUP_MS) {
            return SEN6X_CO2_WARMUP_MS - sincePmMeasureStarted;
        }
        return 0;
    }
    case SENXX_MEASUREMENT_2: {
        if (sincePmMeasureStarted < SENXX_PM_WARMUP_MS_2) {
            // Report how many seconds are pending to cover the first warm up period
            return SENXX_PM_WARMUP_MS_2 - sincePmMeasureStarted;
        }
        return 0;
    }
    case SENXX_CLEANING: {
        uint32_t sinceCleaningStarted = now - cleaningStarted;
        if (sinceCleaningStarted < SENXX_CLEANING_DURATION_MS) {
            return SENXX_CLEANING_DURATION_MS - sinceCleaningStarted;
        }
        finishCleaning();
        return 0;
    }
    default: {
        return -1;
    }
    }
}

bool SENXXSensor::getMetrics(meshtastic_Telemetry *measurement)
{
    LOG_INFO("%s: Attempting to get metrics", sensorName);
    if (!isActive()) {
        LOG_INFO("%s: not in measurement mode", sensorName);
        return false;
    }

#ifdef SENXX_I2C_CLOCK_SPEED
    ReClockI2CGuard clockGuard(reClockI2C, SENXX_I2C_CLOCK_SPEED);
#endif /* SENXX_I2C_CLOCK_SPEED */

    uint8_t response;
    response = getMeasurements();

    if (response == 0) {
        if (senxxmeasurement.pM1p0 != UINT16_MAX) {
            measurement->variant.air_quality_metrics.has_pm10_standard = true;
            measurement->variant.air_quality_metrics.pm10_standard = senxxmeasurement.pM1p0;
        }
        if (senxxmeasurement.pM2p5 != UINT16_MAX) {
            measurement->variant.air_quality_metrics.has_pm25_standard = true;
            measurement->variant.air_quality_metrics.pm25_standard = senxxmeasurement.pM2p5;
        }
        if (senxxmeasurement.pM4p0 != UINT16_MAX) {
            measurement->variant.air_quality_metrics.has_pm40_standard = true;
            measurement->variant.air_quality_metrics.pm40_standard = senxxmeasurement.pM4p0;
        }
        if (senxxmeasurement.pM10p0 != UINT16_MAX) {
            measurement->variant.air_quality_metrics.has_pm100_standard = true;
            measurement->variant.air_quality_metrics.pm100_standard = senxxmeasurement.pM10p0;
        }
        if (senxxmeasurement.pN0p5 != UINT32_MAX) {
            measurement->variant.air_quality_metrics.has_particles_05um = true;
            measurement->variant.air_quality_metrics.particles_05um = senxxmeasurement.pN0p5;
        }
        if (senxxmeasurement.pN1p0 != UINT32_MAX) {
            measurement->variant.air_quality_metrics.has_particles_10um = true;
            measurement->variant.air_quality_metrics.particles_10um = senxxmeasurement.pN1p0;
        }
        if (senxxmeasurement.pN2p5 != UINT32_MAX) {
            measurement->variant.air_quality_metrics.has_particles_25um = true;
            measurement->variant.air_quality_metrics.particles_25um = senxxmeasurement.pN2p5;
        }
        if (senxxmeasurement.pN4p0 != UINT32_MAX) {
            measurement->variant.air_quality_metrics.has_particles_40um = true;
            measurement->variant.air_quality_metrics.particles_40um = senxxmeasurement.pN4p0;
        }
        if (senxxmeasurement.pN10p0 != UINT32_MAX) {
            measurement->variant.air_quality_metrics.has_particles_100um = true;
            measurement->variant.air_quality_metrics.particles_100um = senxxmeasurement.pN10p0;
        }
        if (senxxmeasurement.tSize != FLT_MAX) {
            measurement->variant.air_quality_metrics.has_particles_tps = true;
            measurement->variant.air_quality_metrics.particles_tps = senxxmeasurement.tSize;
        }

        if (hasRHT) {
            if (senxxmeasurement.humidity != FLT_MAX) {
                measurement->variant.air_quality_metrics.has_pm_humidity = true;
                measurement->variant.air_quality_metrics.pm_humidity = senxxmeasurement.humidity;
            }
            if (senxxmeasurement.temperature != FLT_MAX) {
                measurement->variant.air_quality_metrics.has_pm_temperature = true;
                measurement->variant.air_quality_metrics.pm_temperature = senxxmeasurement.temperature;
            }
        }

        if (hasVOC && senxxmeasurement.vocIndex != FLT_MAX) {
            measurement->variant.air_quality_metrics.has_pm_voc_idx = true;
            measurement->variant.air_quality_metrics.pm_voc_idx = senxxmeasurement.vocIndex;
        }

        if (hasNOx && senxxmeasurement.noxIndex != FLT_MAX) {
            measurement->variant.air_quality_metrics.has_pm_nox_idx = true;
            measurement->variant.air_quality_metrics.pm_nox_idx = senxxmeasurement.noxIndex;
        }

        if (hasCO2 && senxxmeasurement.co2 != FLT_MAX) {
            measurement->variant.air_quality_metrics.has_co2 = true;
            measurement->variant.air_quality_metrics.co2 = (uint32_t)senxxmeasurement.co2;
        }

        if (hasHCHO && senxxmeasurement.hcho != FLT_MAX) {
            measurement->variant.air_quality_metrics.has_form_formaldehyde = true;
            measurement->variant.air_quality_metrics.form_formaldehyde = senxxmeasurement.hcho;
        }

        if (isSen6xFamily()) {
            uint32_t statusFlags = 0;
            if (readDeviceStatus(statusFlags)) {
                measurement->variant.air_quality_metrics.has_pm_status_flags = true;
                measurement->variant.air_quality_metrics.pm_status_flags = statusFlags;
                logDeviceStatus(statusFlags);
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

bool SENXXSensor::readDeviceStatus(uint32_t &statusFlags)
{
    if (!isSen6xFamily()) {
        return false;
    }

    if (!sendCommand(SEN6X_READ_DEVICE_STATUS)) {
        LOG_ERROR("%s: Error sending read device status command", sensorName);
        return false;
    }
    delay(20); // From Sensirion Datasheet

    uint8_t dataBuffer[4]{};
    size_t receivedNumber = readBuffer(&dataBuffer[0], 6);
    if (receivedNumber == 0) {
        LOG_ERROR("%s: Error getting device status", sensorName);
        return false;
    }

    statusFlags = (static_cast<uint32_t>(dataBuffer[0]) << 24) | (static_cast<uint32_t>(dataBuffer[1]) << 16) |
                  (static_cast<uint32_t>(dataBuffer[2]) << 8) | static_cast<uint32_t>(dataBuffer[3]);
    return true;
}

void SENXXSensor::logDeviceStatus(uint32_t statusFlags)
{
    if (statusFlags & SEN6X_STATUS_FAN_ERROR)
        LOG_ERROR("%s: Fan error", sensorName);
    if (statusFlags & SEN6X_STATUS_RHT_ERROR)
        LOG_ERROR("%s: RH&T sensor error", sensorName);
    if (statusFlags & SEN6X_STATUS_GAS_ERROR)
        LOG_ERROR("%s: Gas (VOC/NOx) sensor error", sensorName);
    if (statusFlags & SEN6X_STATUS_CO2_2_ERROR)
        LOG_ERROR("%s: CO2 sensor error", sensorName);
    if (statusFlags & SEN6X_STATUS_HCHO_ERROR)
        LOG_ERROR("%s: Formaldehyde sensor error", sensorName);
    if (statusFlags & SEN6X_STATUS_PM_ERROR)
        LOG_ERROR("%s: PM sensor error", sensorName);
    if (statusFlags & SEN6X_STATUS_CO2_1_ERROR)
        LOG_ERROR("%s: CO2 sensor error", sensorName);
    if (statusFlags & SEN6X_STATUS_FAN_SPEED_WARNING)
        LOG_WARN("%s: Fan speed warning", sensorName);
}

bool SENXXSensor::setTemperatureOffset(float tempReference)
{
    if (!isSen6xFamily()) {
        // No verified opcode for SEN5X's temperature offset command yet.
        LOG_WARN("%s: Temperature offset not implemented for this model", sensorName);
        return false;
    }

    if (senxxmeasurement.temperature == FLT_MAX) {
        LOG_ERROR("%s: No recent temperature reading to calibrate against", sensorName);
        return false;
    }

    float tempOffset = senxxmeasurement.temperature - tempReference;
    LOG_INFO("%s: Setting temperature offset: %.2f (current=%.2f, reference=%.2f)", sensorName, tempOffset,
             senxxmeasurement.temperature, tempReference);

    // Payload: offset (int16, *200), slope (int16, *10000, 0=no change over time),
    // time constant (uint16 seconds, 0=apply immediately), slot (uint16, 0=base self-heating).
    int16_t offsetWord = static_cast<int16_t>(tempOffset * 200.0f);
    uint8_t buffer[8]{
        static_cast<uint8_t>((offsetWord >> 8) & 0xFF),
        static_cast<uint8_t>(offsetWord & 0xFF),
        0,
        0, // slope = 0
        0,
        0, // time constant = 0 (apply immediately)
        0,
        0, // slot = 0
    };

    if (!sendCommand(SEN6X_GET_SET_TEMP_OFFSET, buffer, 8)) {
        LOG_ERROR("%s: Error setting temperature offset", sensorName);
        return false;
    }

    return true;
}

bool SENXXSensor::co2PerformFRC(uint32_t targetCO2ppm)
{
    if (!hasCO2) {
        return false;
    }

    LOG_INFO("%s: Issuing FRC. Ensure device has been working at least 3 minutes in stable target environment", sensorName);
    LOG_INFO("%s: Target CO2: %u ppm", sensorName, targetCO2ppm);

    uint8_t buffer[2]{static_cast<uint8_t>((targetCO2ppm >> 8) & 0xFF), static_cast<uint8_t>(targetCO2ppm & 0xFF)};
    if (!sendCommand(SEN6X_PERFORM_FORCED_CO2_RECAL, buffer, 2)) {
        LOG_ERROR("%s: Error sending forced recalibration command", sensorName);
        return false;
    }
    delay(500); // From Sensirion Datasheet

    uint8_t resultBuffer[2]{};
    if (readBuffer(&resultBuffer[0], 3) == 0) {
        LOG_ERROR("%s: Error reading forced recalibration result", sensorName);
        return false;
    }

    uint16_t correction = static_cast<uint16_t>((resultBuffer[0] << 8) | resultBuffer[1]);
    if (correction == 0xFFFF) {
        LOG_ERROR("%s: Forced recalibration failed", sensorName);
        return false;
    }

    LOG_INFO("%s: FRC correction successful. Correction output: %d ppm", sensorName, (int32_t)correction - 0x8000);
    return true;
}

bool SENXXSensor::co2GetASC(bool &ascEnabled)
{
    if (!hasCO2) {
        return false;
    }

    if (!sendCommand(SEN6X_GET_SET_CO2_ASC)) {
        LOG_ERROR("%s: Error sending get ASC command", sensorName);
        return false;
    }
    delay(20); // From Sensirion Datasheet

    uint8_t buffer[2]{};
    if (readBuffer(&buffer[0], 3) == 0) {
        LOG_ERROR("%s: Error reading ASC status", sensorName);
        return false;
    }

    ascEnabled = buffer[1] != 0;
    LOG_INFO("%s: ASC is %s", sensorName, ascEnabled ? "enabled" : "disabled");
    return true;
}

bool SENXXSensor::co2SetASC(bool ascEnabled)
{
    if (!hasCO2) {
        return false;
    }

    LOG_INFO("%s: %s ASC", sensorName, ascEnabled ? "Enabling" : "Disabling");

    uint8_t buffer[2]{0, static_cast<uint8_t>(ascEnabled ? 1 : 0)};
    if (!sendCommand(SEN6X_GET_SET_CO2_ASC, buffer, 2)) {
        LOG_ERROR("%s: Error setting ASC", sensorName);
        return false;
    }
    return true;
}

bool SENXXSensor::co2SetAltitude(uint32_t altitude)
{
    if (!hasCO2) {
        return false;
    }

    LOG_INFO("%s: Setting altitude at %um (volatile - reverts on device reset)", sensorName, altitude);

    uint16_t altitudeWord = static_cast<uint16_t>(altitude);
    uint8_t buffer[2]{static_cast<uint8_t>((altitudeWord >> 8) & 0xFF), static_cast<uint8_t>(altitudeWord & 0xFF)};
    if (!sendCommand(SEN6X_GET_SET_ALTITUDE, buffer, 2)) {
        LOG_ERROR("%s: Error setting altitude", sensorName);
        return false;
    }
    return true;
}

bool SENXXSensor::co2SetAmbientPressure(uint32_t ambientPressurePa)
{
    if (!hasCO2) {
        return false;
    }

    // The SEN6X command expects hPa (700-1200), while the admin config field
    // matches SCD4X's Pa convention (70000-120000) for consistency across sensors.
    uint16_t pressureHpa = static_cast<uint16_t>(ambientPressurePa / 100);
    LOG_INFO("%s: Setting ambient pressure at %u hPa (volatile - reverts on device reset)", sensorName, pressureHpa);

    uint8_t buffer[2]{static_cast<uint8_t>((pressureHpa >> 8) & 0xFF), static_cast<uint8_t>(pressureHpa & 0xFF)};
    if (!sendCommand(SEN6X_GET_SET_AMBIENT_PRESSURE, buffer, 2)) {
        LOG_ERROR("%s: Error setting ambient pressure", sensorName);
        return false;
    }
    return true;
}

bool SENXXSensor::co2FactoryReset()
{
    if (!hasCO2) {
        return false;
    }

    LOG_INFO("%s: Requesting CO2 sensor factory reset", sensorName);
    if (!sendCommand(SEN6X_CO2_FACTORY_RESET)) {
        LOG_ERROR("%s: Error requesting CO2 factory reset", sensorName);
        return false;
    }
    return true;
}

void SENXXSensor::setMode(bool setOneShot)
{
    oneShotMode = setOneShot;
    if (oneShotMode) {
        LOG_INFO("%s: setting mode to one shot mode", sensorName);
    } else {
        LOG_INFO("%s: setting mode to continuous mode", sensorName);
    }
}

AdminMessageHandleResult SENXXSensor::handleAdminMessage(const meshtastic_MeshPacket &mp, meshtastic_AdminMessage *request,
                                                         meshtastic_AdminMessage *response)
{
    AdminMessageHandleResult result;
    result = AdminMessageHandleResult::NOT_HANDLED;

    switch (request->which_payload_variant) {
    case meshtastic_AdminMessage_sensor_config_tag: {
#ifdef SENXX_I2C_CLOCK_SPEED
        ReClockI2CGuard clockGuard(reClockI2C, SENXX_I2C_CLOCK_SPEED);
#endif /* SENXX_I2C_CLOCK_SPEED */
        bool ok = true;
        bool wasActive = isActive();

        if (isSen6xFamily()) {
            if (!request->sensor_config.has_sen6x_config) {
                result = AdminMessageHandleResult::NOT_HANDLED;
                break;
            }
            const auto &cfg = request->sensor_config.sen6x_config;

            if (cfg.has_set_one_shot_mode) {
                this->setMode(cfg.set_one_shot_mode);
            }

            if (cfg.has_start_fan_cleaning && cfg.start_fan_cleaning) {
                ok &= this->startCleaning();
            }

            // FRC/ASC/altitude are only valid in idle mode (see SEN6X datasheet), and the
            // temperature offset command doesn't need measurement running either - stop
            // once, run every requested calibration step, then resume if we were active.
            bool needsCalibration = cfg.has_set_temperature || cfg.has_set_asc || cfg.has_set_altitude ||
                                    cfg.has_set_ambient_pressure || cfg.has_factory_reset;
            if (needsCalibration && state == SENXX_CLEANING) {
                // A fan cleaning was just started above (non-blocking) - stopping measurement
                // now would interrupt it. Calibration and cleaning can't be requested together;
                // ask the caller to retry once the cleaning cycle completes.
                LOG_WARN("%s: Skipping calibration request - fan cleaning in progress, retry once it completes", sensorName);
                ok = false;
            } else if (needsCalibration) {
                if (wasActive) {
                    sendCommand(SENXX_STOP_MEASUREMENT);
                    delay(1400); // From Sensirion Datasheet
                }

                if (cfg.has_set_temperature) {
                    ok &= this->setTemperatureOffset(cfg.set_temperature);
                }

                if (hasCO2 &&
                    (cfg.has_set_asc || cfg.has_set_altitude || cfg.has_set_ambient_pressure || cfg.has_factory_reset)) {
                    Co2AdminRequest co2req;
                    // Matches SCD4X_config's own convention: presence of the field (not its
                    // value) is what requests a factory reset.
                    co2req.hasFactoryReset = cfg.has_factory_reset;
                    co2req.hasSetAsc = cfg.has_set_asc;
                    co2req.setAsc = cfg.set_asc;
                    co2req.hasTargetCo2 = cfg.has_set_target_co2_conc;
                    co2req.targetCo2 = cfg.set_target_co2_conc;
                    co2req.hasSetAltitude = cfg.has_set_altitude;
                    co2req.setAltitude = cfg.set_altitude;
                    co2req.hasSetAmbientPressure = cfg.has_set_ambient_pressure;
                    co2req.setAmbientPressure = cfg.set_ambient_pressure;
                    ok &= this->handleCo2AdminRequest(co2req, sensorName);
                }

                if (wasActive) {
                    // Not this->wakeUp() - we're already inside this function's own
                    // ReClockI2CGuard, and that guard isn't reentrant (see its comment).
                    this->wakeUpInternal();
                }
            }
        } else {
            if (!request->sensor_config.has_sen5x_config) {
                result = AdminMessageHandleResult::NOT_HANDLED;
                break;
            }
            const auto &cfg = request->sensor_config.sen5x_config;

            if (cfg.has_set_one_shot_mode) {
                this->setMode(cfg.set_one_shot_mode);
            }

            if (cfg.has_start_fan_cleaning && cfg.start_fan_cleaning) {
                ok &= this->startCleaning();
            }
        }

        result = ok ? AdminMessageHandleResult::HANDLED : AdminMessageHandleResult::NOT_HANDLED;
        break;
    }

    default:
        result = AdminMessageHandleResult::NOT_HANDLED;
    }

    return result;
}
#endif
