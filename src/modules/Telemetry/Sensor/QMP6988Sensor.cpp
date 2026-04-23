#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "QMP6988Sensor.h"
#include "TelemetrySensor.h"
#include <Wire.h>

namespace
{

constexpr uint8_t QMP6988_CHIP_ID = 0x5C;
constexpr uint8_t QMP6988_CHIP_ID_REG = 0xD1;
constexpr uint8_t QMP6988_RESET_REG = 0xE0;
constexpr uint8_t QMP6988_CONFIG_REG = 0xF1;
constexpr uint8_t QMP6988_CTRL_MEAS_REG = 0xF4;
constexpr uint8_t QMP6988_PRESSURE_MSB_REG = 0xF7;
constexpr uint8_t QMP6988_CALIBRATION_START_REG = 0xA0;
constexpr uint8_t QMP6988_CALIBRATION_LENGTH = 25;

constexpr uint8_t QMP6988_MODE_NORMAL = 0x03;
constexpr uint8_t QMP6988_FILTER_4 = 0x02;
constexpr uint8_t QMP6988_OVERSAMPLING_1X = 0x01;
constexpr uint8_t QMP6988_OVERSAMPLING_8X = 0x04;

constexpr int32_t QMP6988_SUBTRACTOR = 8388608;

} // namespace

QMP6988Sensor::QMP6988Sensor() : TelemetrySensor(meshtastic_TelemetrySensorType_QMP6988, "QMP6988") {}

bool QMP6988Sensor::writeByte(uint8_t reg, uint8_t value)
{
    bus->beginTransmission(address);
    bus->write(reg);
    bus->write(value);
    return bus->endTransmission() == 0;
}

bool QMP6988Sensor::readByte(uint8_t reg, uint8_t &value)
{
    if (!readBytes(reg, &value, 1)) {
        return false;
    }
    return true;
}

bool QMP6988Sensor::readBytes(uint8_t reg, uint8_t *buffer, uint8_t length)
{
    bus->beginTransmission(address);
    bus->write(reg);
    if (bus->endTransmission() != 0) {
        return false;
    }

    if (bus->requestFrom((int)address, (int)length) != length) {
        return false;
    }

    for (uint8_t i = 0; i < length; ++i) {
        if (!bus->available()) {
            return false;
        }
        buffer[i] = bus->read();
    }

    return true;
}

bool QMP6988Sensor::reset()
{
    if (!writeByte(QMP6988_RESET_REG, 0xE6)) {
        return false;
    }
    delay(20);
    writeByte(QMP6988_RESET_REG, 0x00);
    return true;
}

bool QMP6988Sensor::readCalibrationData()
{
    uint8_t data[QMP6988_CALIBRATION_LENGTH] = {};
    for (uint8_t i = 0; i < sizeof(data); ++i) {
        if (!readBytes(QMP6988_CALIBRATION_START_REG + i, &data[i], 1)) {
            return false;
        }
    }

    calibration.a0 = (int32_t)(((data[18] << 12) | (data[19] << 4) | (data[24] & 0x0F)) << 12) >> 12;
    calibration.a1 = (int16_t)((data[20] << 8) | data[21]);
    calibration.a2 = (int16_t)((data[22] << 8) | data[23]);
    calibration.b00 = (int32_t)(((data[0] << 12) | (data[1] << 4) | ((data[24] & 0xF0) >> 4)) << 12) >> 12;
    calibration.bt1 = (int16_t)((data[2] << 8) | data[3]);
    calibration.bt2 = (int16_t)((data[4] << 8) | data[5]);
    calibration.bp1 = (int16_t)((data[6] << 8) | data[7]);
    calibration.b11 = (int16_t)((data[8] << 8) | data[9]);
    calibration.bp2 = (int16_t)((data[10] << 8) | data[11]);
    calibration.b12 = (int16_t)((data[12] << 8) | data[13]);
    calibration.b21 = (int16_t)((data[14] << 8) | data[15]);
    calibration.bp3 = (int16_t)((data[16] << 8) | data[17]);

    integerCalibration.a0 = calibration.a0;
    integerCalibration.b00 = calibration.b00;
    integerCalibration.a1 = 3608L * calibration.a1 - 1731677965L;
    integerCalibration.a2 = 16889L * calibration.a2 - 87619360L;
    integerCalibration.bt1 = 2982LL * calibration.bt1 + 107370906LL;
    integerCalibration.bt2 = 329854LL * calibration.bt2 + 108083093LL;
    integerCalibration.bp1 = 19923LL * calibration.bp1 + 1133836764LL;
    integerCalibration.b11 = 2406LL * calibration.b11 + 118215883LL;
    integerCalibration.bp2 = 3079LL * calibration.bp2 - 181579595LL;
    integerCalibration.b12 = 6846LL * calibration.b12 + 85590281LL;
    integerCalibration.b21 = 13836LL * calibration.b21 + 79333336LL;
    integerCalibration.bp3 = 2915LL * calibration.bp3 + 157155561LL;

    return true;
}

void QMP6988Sensor::setPowerMode(uint8_t powerMode)
{
    uint8_t control = 0;
    if (!readByte(QMP6988_CTRL_MEAS_REG, control)) {
        return;
    }

    control &= 0xFC;
    control |= (powerMode & 0x03);
    writeByte(QMP6988_CTRL_MEAS_REG, control);
    delay(20);
}

void QMP6988Sensor::setFilter(uint8_t filter)
{
    writeByte(QMP6988_CONFIG_REG, filter & 0x03);
    delay(20);
}

void QMP6988Sensor::setOversamplingPressure(uint8_t oversampling)
{
    uint8_t control = 0;
    if (!readByte(QMP6988_CTRL_MEAS_REG, control)) {
        return;
    }

    control &= 0xE3;
    control |= (oversampling << 2);
    writeByte(QMP6988_CTRL_MEAS_REG, control);
    delay(20);
}

void QMP6988Sensor::setOversamplingTemperature(uint8_t oversampling)
{
    uint8_t control = 0;
    if (!readByte(QMP6988_CTRL_MEAS_REG, control)) {
        return;
    }

    control &= 0x1F;
    control |= (oversampling << 5);
    writeByte(QMP6988_CTRL_MEAS_REG, control);
    delay(20);
}

int16_t QMP6988Sensor::convertTemperature(int32_t rawTemperature) const
{
    int64_t wk1 = (int64_t)integerCalibration.a1 * rawTemperature;
    int64_t wk2 = ((int64_t)integerCalibration.a2 * rawTemperature) >> 14;
    wk2 = (wk2 * rawTemperature) >> 10;
    wk2 = ((wk1 + wk2) / 32767) >> 19;
    return (int16_t)((integerCalibration.a0 + wk2) >> 4);
}

int32_t QMP6988Sensor::convertPressure(int32_t rawPressure, int16_t compensatedTemperature) const
{
    int64_t wk1 = (int64_t)integerCalibration.bt1 * compensatedTemperature;
    int64_t wk2 = ((int64_t)integerCalibration.bp1 * rawPressure) >> 5;
    wk1 += wk2;

    wk2 = ((int64_t)integerCalibration.bt2 * compensatedTemperature) >> 1;
    wk2 = (wk2 * compensatedTemperature) >> 8;
    int64_t wk3 = wk2;

    wk2 = ((int64_t)integerCalibration.b11 * compensatedTemperature) >> 4;
    wk2 = (wk2 * rawPressure) >> 1;
    wk3 += wk2;

    wk2 = ((int64_t)integerCalibration.bp2 * rawPressure) >> 13;
    wk2 = (wk2 * rawPressure) >> 1;
    wk3 += wk2;
    wk1 += wk3 >> 14;

    wk2 = (int64_t)integerCalibration.b12 * compensatedTemperature;
    wk2 = (wk2 * compensatedTemperature) >> 22;
    wk2 = (wk2 * rawPressure) >> 1;
    wk3 = wk2;

    wk2 = ((int64_t)integerCalibration.b21 * compensatedTemperature) >> 6;
    wk2 = (wk2 * rawPressure) >> 23;
    wk2 = (wk2 * rawPressure) >> 1;
    wk3 += wk2;

    wk2 = ((int64_t)integerCalibration.bp3 * rawPressure) >> 12;
    wk2 = (wk2 * rawPressure) >> 23;
    wk2 = wk2 * rawPressure;
    wk3 += wk2;

    wk1 += wk3 >> 15;
    wk1 /= 32767;
    wk1 >>= 11;
    wk1 += integerCalibration.b00;

    return (int32_t)wk1;
}

bool QMP6988Sensor::readRawMeasurement(int32_t &rawPressure, int32_t &rawTemperature)
{
    uint8_t data[6] = {};
    if (!readBytes(QMP6988_PRESSURE_MSB_REG, data, sizeof(data))) {
        return false;
    }

    uint32_t pressure = ((uint32_t)data[0] << 16) | ((uint16_t)data[1] << 8) | data[2];
    uint32_t temperature = ((uint32_t)data[3] << 16) | ((uint16_t)data[4] << 8) | data[5];

    rawPressure = (int32_t)pressure - QMP6988_SUBTRACTOR;
    rawTemperature = (int32_t)temperature - QMP6988_SUBTRACTOR;
    return true;
}

bool QMP6988Sensor::initDevice(TwoWire *bus, ScanI2C::FoundDevice *dev)
{
    LOG_INFO("Init sensor: %s", sensorName);

    this->bus = bus;
    address = dev->address.address;

    uint8_t chipId = 0;
    if (!readByte(QMP6988_CHIP_ID_REG, chipId) || chipId != QMP6988_CHIP_ID) {
        LOG_ERROR("%s: chip id mismatch at 0x%x", sensorName, address);
        initI2CSensor();
        return status;
    }

    if (!reset()) {
        LOG_DEBUG("%s: reset failed at 0x%x, continuing without reset", sensorName, address);
    }

    if (!readCalibrationData()) {
        LOG_ERROR("%s: calibration read failed at 0x%x", sensorName, address);
        initI2CSensor();
        return status;
    }

    status = true;
    if (!status) {
        LOG_ERROR("%s: init failed at 0x%x", sensorName, address);
        initI2CSensor();
        return status;
    }

    setPowerMode(QMP6988_MODE_NORMAL);
    setFilter(QMP6988_FILTER_4);
    setOversamplingPressure(QMP6988_OVERSAMPLING_8X);
    setOversamplingTemperature(QMP6988_OVERSAMPLING_1X);

    initI2CSensor();
    return status;
}

bool QMP6988Sensor::getMetrics(meshtastic_Telemetry *measurement)
{
    int32_t rawPressure = 0;
    int32_t rawTemperature = 0;

    if (!readRawMeasurement(rawPressure, rawTemperature)) {
        LOG_ERROR("%s: read sample failed", sensorName);
        return false;
    }

    int16_t compensatedTemperature = convertTemperature(rawTemperature);
    int32_t compensatedPressure = convertPressure(rawPressure, compensatedTemperature);

    measurement->variant.environment_metrics.has_temperature = true;
    measurement->variant.environment_metrics.has_barometric_pressure = true;
    measurement->variant.environment_metrics.temperature = (float)compensatedTemperature / 256.0f;
    measurement->variant.environment_metrics.barometric_pressure = (float)compensatedPressure / 16.0f;

    LOG_INFO("%s: Got temp:%fdegC pressure:%fhPa", sensorName, measurement->variant.environment_metrics.temperature,
             measurement->variant.environment_metrics.barometric_pressure);

    return true;
}

#endif
