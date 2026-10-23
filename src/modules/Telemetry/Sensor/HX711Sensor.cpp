#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && defined(PIN_HX711_DATA) && defined(PIN_HX711_SCK)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "FSCommon.h"
#include "HX711Sensor.h"
#include "SPILock.h"
#include "SafeFile.h"
#include "TelemetrySensor.h"

#include <Arduino.h>
#include <pb_decode.h>
#include <pb_encode.h>

namespace
{
constexpr uint32_t HX711_CLOCK_DELAY_US = 1;

// Total timeout for a full averaged measurement (must allow for all samples)
constexpr uint32_t HX711_MEASUREMENT_TIMEOUT_MS = 2000;

// If the calibration factor has never been set, report raw counts as if kg
// so the user can see *something* until calibration is performed.
constexpr float HX711_DEFAULT_CALIBRATION_FACTOR = 1.0f;
} // namespace

// Reuse the NAU7802 config protobuf struct for HX711 flash storage: it holds exactly
// the two fields we need (zeroOffset int32, calibrationFactor float) and is a purely
// local file format, so no protocol changes are required. We keep our own instance
// so this file has no link-time dependency on NAU7802Sensor.cpp.
static meshtastic_Nau7802Config hx711config = meshtastic_Nau7802Config_init_zero;

HX711Sensor::HX711Sensor() : TelemetrySensor(meshtastic_TelemetrySensorType_SENSOR_UNSET, "HX711") {}

bool HX711Sensor::initDevice(TwoWire *bus, ScanI2C::FoundDevice *dev)
{
    (void)bus;
    (void)dev;

    LOG_INFO("Init sensor: %s", sensorName);

    pinMode(PIN_HX711_DATA, INPUT);
    pinMode(PIN_HX711_SCK, OUTPUT);
    digitalWrite(PIN_HX711_SCK, LOW);

    if (!waitReady(1000)) {
        LOG_WARN("HX711 not ready");
        return false;
    }

    if (!loadCalibrationData()) {
        LOG_INFO("No saved HX711 calibration found; using defaults (tare recommended)");
    }

    LOG_INFO("HX711 ready: DOUT=%d SCK=%d, zeroOffset=%ld, calFactor=%.2f", PIN_HX711_DATA, PIN_HX711_SCK, (long)zeroOffset,
             calibrationFactor);
    return true;
}

bool HX711Sensor::waitReady(uint32_t timeoutMs)
{
    const uint32_t start = millis();

    while (digitalRead(PIN_HX711_DATA) != LOW) {
        if ((uint32_t)(millis() - start) >= timeoutMs) {
            return false;
        }

        delay(1);
    }

    return true;
}

bool HX711Sensor::readRaw(int32_t &value, uint32_t timeoutMs)
{
    if (!waitReady(timeoutMs)) {
        return false;
    }

    uint32_t data = 0;

    // PD_SCK must never stay high longer than 60 us or the chip powers down and
    // the sample is corrupted. A FreeRTOS context switch or ISR between SCK
    // high and low can exceed that, so bracket each pulse with
    // noInterrupts()/interrupts() and keep bit processing outside the critical
    // section (same approach as common HX711 libraries).
    for (uint8_t i = 0; i < 24; i++) {
        noInterrupts();
        digitalWrite(PIN_HX711_SCK, HIGH);
        delayMicroseconds(HX711_CLOCK_DELAY_US);
        bool bit = digitalRead(PIN_HX711_DATA);
        digitalWrite(PIN_HX711_SCK, LOW);
        interrupts();

        data = (data << 1) | (bit ? 1 : 0);
        delayMicroseconds(HX711_CLOCK_DELAY_US);
    }

    // One additional pulse selects Channel A, gain 128
    noInterrupts();
    digitalWrite(PIN_HX711_SCK, HIGH);
    delayMicroseconds(HX711_CLOCK_DELAY_US);
    digitalWrite(PIN_HX711_SCK, LOW);
    interrupts();
    delayMicroseconds(HX711_CLOCK_DELAY_US);

    // Sign-extend the 24-bit two's-complement value.
    if (data & 0x800000UL) {
        data |= 0xFF000000UL;
    }

    value = static_cast<int32_t>(data);

    return true;
}

bool HX711Sensor::readAverage(int32_t &average, uint32_t timeoutMs)
{
    // Discard the first sample (may be stale from a previous conversion cycle)
    int32_t scratch = 0;
    if (!readRaw(scratch, timeoutMs)) {
        return false;
    }

    const uint32_t start = millis();

    int64_t sum = 0;
    for (uint8_t i = 0; i < SAMPLES_TO_AVERAGE; i++) {
        int32_t sample = 0;
        if (!readRaw(sample, timeoutMs)) {
            return false;
        }
        sum += sample;
    }

    if ((uint32_t)(millis() - start) >= HX711_MEASUREMENT_TIMEOUT_MS) {
        return false;
    }

    average = static_cast<int32_t>(sum / SAMPLES_TO_AVERAGE);
    return true;
}

bool HX711Sensor::getMetrics(meshtastic_Telemetry *measurement)
{
    int32_t average = 0;

    if (!readAverage(average, 1000)) {
        LOG_WARN("HX711 conversion timeout");
        return false;
    }

    const float weightKg = (average - zeroOffset) / calibrationFactor;

    LOG_INFO("HX711 raw avg: %ld -> %.3f kg (offset=%ld, factor=%.2f)", (long)average, weightKg, (long)zeroOffset,
             calibrationFactor);

    measurement->variant.environment_metrics.has_weight = true;
    measurement->variant.environment_metrics.weight = weightKg;
    return true;
}

void HX711Sensor::tare()
{
    int32_t average = 0;
    if (!readAverage(average, 1000)) {
        LOG_WARN("HX711 tare failed: conversion timeout");
        return;
    }

    zeroOffset = average;
    if (!saveCalibrationData()) {
        LOG_WARN("Failed to save calibration data");
    }
    LOG_INFO("HX711 tared. Offset: %ld", (long)zeroOffset);
}

void HX711Sensor::calibrate(float weightKg)
{
    int32_t average = 0;
    if (!readAverage(average, 1000)) {
        LOG_WARN("HX711 calibration failed: conversion timeout");
        return;
    }

    const int32_t net = average - zeroOffset;
    if (net == 0 || weightKg <= 0) {
        LOG_WARN("HX711 calibration needs a nonzero net reading and a positive known weight");
        return;
    }

    calibrationFactor = static_cast<float>(net) / weightKg;
    if (!saveCalibrationData()) {
        LOG_WARN("Failed to save calibration data");
    }
    LOG_INFO("HX711 calibrated: %.2f kg -> factor %.2f counts/kg", weightKg, calibrationFactor);
}

AdminMessageHandleResult HX711Sensor::handleAdminMessage(const meshtastic_MeshPacket &mp, meshtastic_AdminMessage *request,
                                                         meshtastic_AdminMessage *response)
{
    (void)mp;
    (void)response;

    AdminMessageHandleResult result;

    switch (request->which_payload_variant) {
    case meshtastic_AdminMessage_set_scale_tag:
        if (request->set_scale == 0) {
            this->tare();
            LOG_DEBUG("Client requested to tare scale");
        } else {
            this->calibrate(static_cast<float>(request->set_scale));
            LOG_DEBUG("Client requested to calibrate to %d kg", request->set_scale);
        }
        result = AdminMessageHandleResult::HANDLED;
        break;

    default:
        result = AdminMessageHandleResult::NOT_HANDLED;
    }

    return result;
}

bool HX711Sensor::saveCalibrationData()
{
    auto file = SafeFile(hx711ConfigFileName);
    hx711config.zeroOffset = zeroOffset;
    hx711config.calibrationFactor = calibrationFactor;
    bool okay = false;

    LOG_INFO("%s state write to %s", sensorName, hx711ConfigFileName);
    pb_ostream_t stream = {&writecb, static_cast<Print *>(&file), meshtastic_Nau7802Config_size};

    if (!pb_encode(&stream, &meshtastic_Nau7802Config_msg, &hx711config)) {
        LOG_ERROR("Can't encode protobuf %s", PB_GET_ERROR(&stream));
    } else {
        okay = true;
    }
    // Note: SafeFile::close() already acquires the lock and releases it internally
    okay &= file.close();

    return okay;
}

bool HX711Sensor::loadCalibrationData()
{
    spiLock->lock();
    auto file = FSCom.open(hx711ConfigFileName, FILE_O_READ);
    bool okay = false;
    if (file) {
        LOG_INFO("%s state read from %s", sensorName, hx711ConfigFileName);
        pb_istream_t stream = {&readcb, &file, meshtastic_Nau7802Config_size};
        if (!pb_decode(&stream, &meshtastic_Nau7802Config_msg, &hx711config)) {
            LOG_ERROR("Can't decode protobuf %s", PB_GET_ERROR(&stream));
        } else {
            zeroOffset = hx711config.zeroOffset;
            calibrationFactor = hx711config.calibrationFactor;
            // A negative factor is valid (reversed load-cell polarity); only
            // replace zero/NaN values so the calibration survives reboots.
            if (calibrationFactor == 0 || !isfinite(calibrationFactor)) {
                calibrationFactor = HX711_DEFAULT_CALIBRATION_FACTOR;
            }
            okay = true;
        }
        file.close();
    } else {
        LOG_INFO("No %s state found (File: %s)", sensorName, hx711ConfigFileName);
    }
    spiLock->unlock();
    return okay;
}

#endif
