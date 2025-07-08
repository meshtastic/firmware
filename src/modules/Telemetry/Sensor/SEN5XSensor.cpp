#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "SEN5XSensor.h"
#include "TelemetrySensor.h"

SEN5XSensor::SEN5XSensor() : TelemetrySensor(meshtastic_TelemetrySensorType_SEN5X, "SEN5X") {}

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

    LOG_INFO("SEN5X Firmware Version: %d", firmwareVer);
    LOG_INFO("SEN5X Hardware Version: %d", hardwareVer);
    LOG_INFO("SEN5X Protocol Version: %d", protocolVer);

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
            uint8_t calcCRC = CRC(&buffer[bi - 2]);
            toSend[i++] = calcCRC;
        }
    }

    // Transmit the data
    LOG_INFO("Beginning connection to SEN5X: 0x%x", address);
    bus->beginTransmission(address);
    size_t writtenBytes = bus->write(toSend, bufferSize);
    uint8_t i2c_error = bus->endTransmission();

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
    size_t readedBytes = bus->requestFrom(address, byteNumber);

    if (readedBytes != byteNumber) {
        LOG_ERROR("SEN5X: Error reading I2C bus");
        return 0;
    }

    uint8_t i = 0;
    uint8_t receivedBytes = 0;
    while (readedBytes > 0) {
        buffer[i++] = bus->read(); // Just as a reminder: i++ returns i and after that increments.
        buffer[i++] = bus->read();
        uint8_t recvCRC = bus->read();
        uint8_t calcCRC = CRC(&buffer[i - 2]);
        if (recvCRC != calcCRC) {
            LOG_ERROR("SEN5X: Checksum error while receiving msg");
            return 0;
        }
        readedBytes -=3;
        receivedBytes += 2;
    }

    return receivedBytes;
}

uint8_t SEN5XSensor::CRC(uint8_t* buffer)
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

int32_t SEN5XSensor::runOnce()
{
    state = SEN5X_NOT_DETECTED;
    LOG_INFO("Init sensor: %s", sensorName);
    if (!hasSensor()) {
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    }

    bus = nodeTelemetrySensorsMap[sensorType].second;
    address = (uint8_t)nodeTelemetrySensorsMap[sensorType].first;
    // sen5x.begin(*bus);

    if (!I2Cdetect(bus, address)) {
        LOG_INFO("SEN5X ERROR no device found on adress");
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    }

    delay(25);

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

    // Detection succeeded
    state = SEN5X_IDLE;
    status = 1;
    LOG_INFO("SEN5X Enabled");

    // uint16_t error;
    // char errorMessage[256];
    // error = sen5x.deviceReset();
    // if (error) {
    //     LOG_INFO("Error trying to execute deviceReset(): ");
    //     errorToString(error, errorMessage, 256);
    //     LOG_INFO(errorMessage);
    //     return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    // }

    // error = sen5x.startMeasurement();
    // if (error) {
    //     LOG_INFO("Error trying to execute startMeasurement(): ");
    //     errorToString(error, errorMessage, 256);
    //     LOG_INFO(errorMessage);
    //     return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    // } else {
    //     status = 1;
    // }

    return initI2CSensor();
}

void SEN5XSensor::setup()
{
#ifdef SEN5X_ENABLE_PIN
    pinMode(SEN5X_ENABLE_PIN, OUTPUT);
    digitalWrite(SEN5X_ENABLE_PIN, HIGH);
    delay(25);
#endif /* SEN5X_ENABLE_PIN */
}

#ifdef SEN5X_ENABLE_PIN
// void SEN5XSensor::sleep() {
//     digitalWrite(SEN5X_ENABLE_PIN, LOW);
//     state = SSEN5XState::SEN5X_OFF;
// }

// uint32_t SEN5XSensor::wakeUp() {
//     digitalWrite(SEN5X_ENABLE_PIN, HIGH);
//     state = SEN5XState::SEN5X_IDLE;
//     return SEN5X_WARMUP_MS;
// }
#endif /* SEN5X_ENABLE_PIN */

bool SEN5XSensor::getMetrics(meshtastic_Telemetry *measurement)
{
    uint16_t error;
    char errorMessage[256];

    // Read Measurement
    float massConcentrationPm1p0;
    float massConcentrationPm2p5;
    float massConcentrationPm4p0;
    float massConcentrationPm10p0;
    float ambientHumidity;
    float ambientTemperature;
    float vocIndex;
    float noxIndex;

    // error = sen5x.readMeasuredValues(
    //     massConcentrationPm1p0, massConcentrationPm2p5, massConcentrationPm4p0,
    //     massConcentrationPm10p0, ambientHumidity, ambientTemperature, vocIndex,
    //     noxIndex);

    // if (error) {
    //     LOG_INFO("Error trying to execute readMeasuredValues(): ");
    //     errorToString(error, errorMessage, 256);
    //     LOG_INFO(errorMessage);
    //     return false;
    // }

    // measurement->variant.air_quality_metrics.has_pm10_standard = true;
    // measurement->variant.air_quality_metrics.pm10_standard = massConcentrationPm1p0;
    // measurement->variant.air_quality_metrics.has_pm25_standard = true;
    // measurement->variant.air_quality_metrics.pm25_standard = massConcentrationPm2p5;
    // measurement->variant.air_quality_metrics.has_pm100_standard = true;
    // measurement->variant.air_quality_metrics.pm100_standard = massConcentrationPm10p0;

    return true;
}

#endif