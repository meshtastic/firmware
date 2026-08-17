#include "BHI260APSensor.h"

#if !defined(ARCH_STM32WL) && !MESHTASTIC_EXCLUDE_I2C && defined(T_DECK_MAX) && defined(HAS_BHI260AP) && \
    __has_include(<SensorBHI260AP.hpp>)

#define BOSCH_APP30_SHUTTLE_BHI260_FW
#include <BoschFirmware.h>

#include "detect/ScanI2CTwoWire.h"
#include "platform/extra_variants/t_deck_max/TDeckMaxBoard.h"
#include <cmath>

BHI260APSensor::BHI260APSensor(ScanI2C::FoundDevice foundDevice) : MotionSensor::MotionSensor(foundDevice) {}

BHI260APSensor::~BHI260APSensor()
{
    releaseResources();
}

BHI260APSensor *BHI260APSensor::interruptInstance = nullptr;

void BHI260APSensor::releaseResources()
{
    if (irqAttached) {
        detachInterrupt(t_deck_max::BHI260AP_IRQ_PIN);
        irqAttached = false;
    }

    if (interruptInstance == this)
        interruptInstance = nullptr;

    if (callbackRegistered) {
        sensor.removeResultEvent(BoschVirtualSensor::ACCEL_PASSTHROUGH, accelerationCallback);
        callbackRegistered = false;
    }

    initialized = false;
    dataReadyPending = false;
    tDeckMaxSetImuPower(false);
}

void BHI260APSensor::accelerationCallback(uint8_t sensorId, uint8_t *data, uint32_t length, uint64_t *timestamp,
                                          void *userData)
{
    (void)timestamp;
    auto *instance = static_cast<BHI260APSensor *>(userData);
    if (instance)
        instance->processAcceleration(sensorId, data, length);
}

void BHI260APSensor::processAcceleration(uint8_t sensorId, uint8_t *data, uint32_t length)
{
    if (sensorId != BoschVirtualSensor::ACCEL_PASSTHROUGH || length < 6)
        return;

    bhy2_data_xyz sample = {};
    bhy2_parse_xyz(data, &sample);
    const float scale = sensor.getScaling(sensorId);
    const float x = sample.x * scale;
    const float y = sample.y * scale;
    const float z = sample.z * scale;

    if (!hasBaseline) {
        baselineX = x;
        baselineY = y;
        baselineZ = z;
        hasBaseline = true;
        return;
    }

    const float deltaX = std::fabs(x - baselineX);
    const float deltaY = std::fabs(y - baselineY);
    const float deltaZ = std::fabs(z - baselineZ);
    if (deltaX > t_deck_max::BHI260AP_MOTION_THRESHOLD_G || deltaY > t_deck_max::BHI260AP_MOTION_THRESHOLD_G ||
        deltaZ > t_deck_max::BHI260AP_MOTION_THRESHOLD_G) {
        motionDetected = true;
    }

    baselineX = (baselineX * 0.9f) + (x * 0.1f);
    baselineY = (baselineY * 0.9f) + (y * 0.1f);
    baselineZ = (baselineZ * 0.9f) + (z * 0.1f);
}

void BHI260APSensor::dataReadyISR()
{
    if (interruptInstance)
        interruptInstance->dataReadyPending = true;
}

bool BHI260APSensor::init()
{
    releaseResources();
    tDeckMaxSetImuPower(true);

    TwoWire *wire = ScanI2CTwoWire::fetchI2CBus(device.address);
    if (wire == nullptr) {
        LOG_WARN("BHI260AP I2C bus unavailable");
        releaseResources();
        return false;
    }

    sensor.setPins(t_deck_max::BHI260AP_RESET_PIN);
    sensor.setFirmware(bosch_firmware_image, bosch_firmware_size, false, false);
    sensor.setBootFromFlash(false);
    if (!sensor.begin(*wire, deviceAddress(), t_deck_max::I2C_SDA_PIN, t_deck_max::I2C_SCL_PIN)) {
        LOG_WARN("BHI260AP init failed");
        releaseResources();
        return false;
    }

    if (!sensor.configure(BoschVirtualSensor::ACCEL_PASSTHROUGH, t_deck_max::BHI260AP_SAMPLE_RATE_HZ,
                          t_deck_max::BHI260AP_REPORT_LATENCY_MS)) {
        LOG_WARN("BHI260AP accelerometer configuration failed");
        releaseResources();
        return false;
    }

    if (!sensor.onResultEvent(BoschVirtualSensor::ACCEL_PASSTHROUGH, accelerationCallback, this)) {
        LOG_WARN("BHI260AP accelerometer callback registration failed");
        releaseResources();
        return false;
    }
    callbackRegistered = true;

    pinMode(t_deck_max::BHI260AP_IRQ_PIN, INPUT);
    interruptInstance = this;
    attachInterrupt(t_deck_max::BHI260AP_IRQ_PIN, dataReadyISR, RISING);
    irqAttached = true;
    initialized = true;
    LOG_INFO("BHI260AP initialized at 0x%02X", deviceAddress());
    return true;
}

int32_t BHI260APSensor::runOnce()
{
    if (!initialized)
        return MOTION_SENSOR_CHECK_INTERVAL_MS;

    if (!dataReadyPending)
        return MOTION_SENSOR_CHECK_INTERVAL_MS;

    dataReadyPending = false;
    sensor.update();
    if (motionDetected) {
        motionDetected = false;
        wakeScreen();
        return 500;
    }

    return MOTION_SENSOR_CHECK_INTERVAL_MS;
}

#endif
