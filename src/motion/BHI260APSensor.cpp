#include "BHI260APSensor.h"

#if !defined(ARCH_STM32WL) && !MESHTASTIC_EXCLUDE_I2C && defined(HAS_BHI260AP) && __has_include(<SensorBHI260AP.hpp>)
// Use the normal APP30 image. KLIO is not needed for GNSS/IMU stabilization.
#define BOSCH_APP30_SHUTTLE_BHI260_FW

#include <SensorBHI260AP.hpp>
#include "mesh/Throttle.h"
#include <BoschFirmware.h>

#ifndef BHI260AP_FORWARD_AXIS
#define BHI260AP_FORWARD_AXIS 1
#endif
#ifndef BHI260AP_FORWARD_SIGN
#define BHI260AP_FORWARD_SIGN 1.0f
#endif

#ifdef BHI260AP_INT
static volatile bool BHI_IRQ = false;
#endif

volatile float BHI260APSensor::latestYawDeg = 0.0f;
volatile float BHI260APSensor::latestLinearX = 0.0f;
volatile float BHI260APSensor::latestLinearY = 0.0f;
volatile float BHI260APSensor::latestLinearZ = 0.0f;
volatile uint32_t BHI260APSensor::latestYawMs = 0;
volatile uint32_t BHI260APSensor::latestLinearMs = 0;
volatile float BHI260APSensor::gnssSpeedAnchorKmph = 0.0f;
volatile float BHI260APSensor::speedEstimateKmph = 0.0f;
volatile uint32_t BHI260APSensor::speedAnchorMs = 0;
volatile uint32_t BHI260APSensor::speedIntegrationMs = 0;
volatile bool BHI260APSensor::speedAnchorValid = false;

namespace
{
static inline float wrap360(float degrees)
{
    while (degrees < 0.0f)
        degrees += 360.0f;
    while (degrees >= 360.0f)
        degrees -= 360.0f;
    return degrees;
}
} // namespace

BHI260APSensor::BHI260APSensor(ScanI2C::FoundDevice foundDevice) : MotionSensor::MotionSensor(foundDevice) {}

void BHI260APSensor::onWristTilt(uint8_t, const uint8_t *, uint32_t, uint64_t *, void *user_data)
{
    static_cast<BHI260APSensor *>(user_data)->wakeRequested = true;
}

bool BHI260APSensor::init()
{
    LOG_WARN("Initializing BHI260AP sensor %u", deviceAddress());
    sensor.setFirmware(bosch_firmware_image, bosch_firmware_size, bosch_firmware_type);
    sensor.setBootFromFlash(bosch_firmware_type);
    if (sensor.begin(Wire, deviceAddress())) {
        // Keep the library's default axis mapping. SensorRemap is not part of
        // the SensorBHI260AP API used by this build.
        BoschSensorInfo info = sensor.getSensorInfo();
        LOG_INFO("Product ID     : %02x\n", info.product_id);
        LOG_INFO("Kernel version : %04u\n", info.kernel_version);
        LOG_INFO("User version   : %04u\n", info.user_version);
        LOG_INFO("ROM version    : %04u\n", info.getRomVersion());
        LOG_INFO("Power state    : %s\n", (info.getHostStatus() & BHY2_HST_POWER_STATE) ? "sleeping" : "active");
        LOG_INFO("Host interface : %s\n", (info.getHostStatus() & BHY2_HST_HOST_PROTOCOL) ? "SPI" : "I2C");
        LOG_INFO("Feature status : 0x%02x\n", info.getFeatStatus());

        stepCounter = new SensorStepCounter(sensor);
        gameRotation = new SensorQuaternion(sensor);
        linearAcceleration = new SensorXYZ(SensorBHI260AP::LINEAR_ACCELERATION, sensor);

        // 20 Hz is enough for the e-ink/navigation stabilizer while keeping I2C
        // traffic and CPU wakeups modest on the nRF52840.
        const bool rotationOk = gameRotation->enable(20.0f, 0);
        const bool linearOk = linearAcceleration->enable(20.0f, 0);
        if (!rotationOk)
            LOG_WARN("BHI260AP game rotation vector unavailable");
        if (!linearOk)
            LOG_WARN("BHI260AP linear acceleration unavailable");

#ifdef BHI260AP_INT
        InterruptConfig intConfig;
        sensor.configureInterrupt(intConfig);
        pinMode(BHI260AP_INT, INPUT);
        attachInterrupt(
            BHI260AP_INT, [] { BHI_IRQ = true; }, RISING);
#endif

        // Keep the existing wrist-tilt wake and step counter behavior.
        // The event API requires the Bosch virtual-sensor ID enum rather than
        // its underlying byte value.
        constexpr BoschVirtualSensor::BoschSensorID wristTilt =
            static_cast<BoschVirtualSensor::BoschSensorID>(SensorBHI260AP::WRIST_TILT_GESTURE);
        // SensorDataParseCallback provides a mutable payload pointer, while
        // the event handler does not modify the payload.
        auto wristTiltCallback = [](uint8_t eventId, uint8_t *data, uint32_t dataLen, uint64_t *timestamp,
                                    void *userData) {
            onWristTilt(eventId, data, dataLen, timestamp, userData);
        };
        if (sensor.onResultEvent(wristTilt, wristTiltCallback, this) && sensor.configure(wristTilt, 1.0f, 0)) {
            LOG_DEBUG("BHI260AP wrist tilt wake enabled");
        } else {
            LOG_WARN("BHI260AP firmware has no wrist tilt gesture, motion wake unavailable");
        }
        stepCounter->enable(1.0f, 0);
        LOG_DEBUG("BHI260AP init ok (GRV + linear acceleration enabled)");
        return true;
    }
    LOG_DEBUG("BHI260AP init failed");
    return false;
}

int32_t BHI260APSensor::runOnce()
{
#ifdef BHI260AP_INT
    if (!BHI_IRQ && !Throttle::hasElapsed(lastPollMs, MOTION_SENSOR_IRQ_KEEPALIVE_MS))
        return MOTION_SENSOR_CHECK_INTERVAL_MS;
    BHI_IRQ = false;
    lastPollMs = millis();
#endif

    sensor.update();
    const uint32_t now = millis();

    if (gameRotation && gameRotation->hasUpdated()) {
        gameRotation->toEuler();
        latestYawDeg = wrap360(gameRotation->getHeading());
        latestYawMs = now;
    }

    if (linearAcceleration && linearAcceleration->hasUpdated()) {
        latestLinearX = linearAcceleration->getX();
        latestLinearY = linearAcceleration->getY();
        latestLinearZ = linearAcceleration->getZ();
        latestLinearMs = now;

        // Very short speed bridge only. The L76K sets the anchor; the BHI260
        // integrates the remapped forward linear acceleration at 20 Hz until
        // the next GNSS speed arrives. Bias is suppressed with a deadband and
        // total divergence is bounded so the IMU can never become the long-term
        // speed reference.
        if (speedAnchorValid) {
            constexpr uint32_t SPEED_BRIDGE_MAX_MS = 3000U;
            constexpr float ACCEL_DEADBAND_MPS2 = 0.12f;
            constexpr float MAX_SPEED_DIVERGENCE_KMPH = 15.0f;
            const uint32_t anchorAgeMs = (uint32_t)(now - speedAnchorMs);
            if (anchorAgeMs <= SPEED_BRIDGE_MAX_MS) {
                uint32_t dtMs = (speedIntegrationMs == 0) ? 0U : (uint32_t)(now - speedIntegrationMs);
                // Ignore long scheduling gaps rather than integrating one stale
                // acceleration sample over an unrealistic interval.
                if (dtMs > 0U && dtMs <= 200U) {
                    float forwardAccel = 0.0f;
#if BHI260AP_FORWARD_AXIS == 0
                    forwardAccel = latestLinearX * BHI260AP_FORWARD_SIGN;
#elif BHI260AP_FORWARD_AXIS == 2
                    forwardAccel = latestLinearZ * BHI260AP_FORWARD_SIGN;
#else
                    forwardAccel = latestLinearY * BHI260AP_FORWARD_SIGN;
#endif
                    if (forwardAccel > -ACCEL_DEADBAND_MPS2 && forwardAccel < ACCEL_DEADBAND_MPS2)
                        forwardAccel = 0.0f;

                    float estimate = speedEstimateKmph + forwardAccel * (dtMs * 0.001f) * 3.6f;
                    if (estimate < 0.0f)
                        estimate = 0.0f;

                    const float minAllowed = (gnssSpeedAnchorKmph > MAX_SPEED_DIVERGENCE_KMPH)
                                                 ? (gnssSpeedAnchorKmph - MAX_SPEED_DIVERGENCE_KMPH)
                                                 : 0.0f;
                    const float maxAllowed = gnssSpeedAnchorKmph + MAX_SPEED_DIVERGENCE_KMPH;
                    if (estimate < minAllowed)
                        estimate = minAllowed;
                    else if (estimate > maxAllowed)
                        estimate = maxAllowed;

                    // At an already-near-zero GNSS anchor, suppress tiny residual
                    // IMU motion so a stationary node stays at zero.
                    if (gnssSpeedAnchorKmph < 0.8f && forwardAccel == 0.0f)
                        estimate = 0.0f;
                    speedEstimateKmph = estimate;
                }
                speedIntegrationMs = now;
            } else {
                speedAnchorValid = false;
            }
        }
    }

    if (stepCounter && stepCounter->hasUpdated()) {
        steps = stepCounter->getStepCount();
        LOG_WARN("Step count updated: %u", steps);
        if (screen)
            screen->steps = steps;
    }
    if (wakeRequested) {
        wakeRequested = false;
        wakeScreen();
    }

    // T-Echo Plus has no connected BHI interrupt line. For IMU fusion we must
    // drain the FIFO substantially faster than the old 1 s fallback poll.
    return MOTION_SENSOR_CHECK_INTERVAL_MS;
}

bool BHI260APSensor::getLatestYawDegrees(float &yawDeg, uint32_t &ageMs)
{
    const uint32_t sampleMs = latestYawMs;
    if (sampleMs == 0)
        return false;
    yawDeg = latestYawDeg;
    ageMs = (uint32_t)(millis() - sampleMs);
    return true;
}

bool BHI260APSensor::getLatestLinearAcceleration(float &x, float &y, float &z, uint32_t &ageMs)
{
    const uint32_t sampleMs = latestLinearMs;
    if (sampleMs == 0)
        return false;
    x = latestLinearX;
    y = latestLinearY;
    z = latestLinearZ;
    ageMs = (uint32_t)(millis() - sampleMs);
    return true;
}

bool BHI260APSensor::getForwardAcceleration(float &accelMps2, uint32_t &ageMs)
{
    float x = 0.0f, y = 0.0f, z = 0.0f;
    if (!getLatestLinearAcceleration(x, y, z, ageMs))
        return false;

#if BHI260AP_FORWARD_AXIS == 0
    accelMps2 = x * BHI260AP_FORWARD_SIGN;
#elif BHI260AP_FORWARD_AXIS == 2
    accelMps2 = z * BHI260AP_FORWARD_SIGN;
#else
    accelMps2 = y * BHI260AP_FORWARD_SIGN;
#endif
    return true;
}
void BHI260APSensor::setGnssSpeedAnchor(float speedKmph)
{
    if (speedKmph < 0.0f)
        speedKmph = 0.0f;
    gnssSpeedAnchorKmph = speedKmph;
    speedEstimateKmph = speedKmph;
    speedAnchorMs = millis();
    speedIntegrationMs = speedAnchorMs;
    speedAnchorValid = true;
}

bool BHI260APSensor::getBridgedSpeedKmph(float &speedKmph, uint32_t &anchorAgeMs)
{
    if (!speedAnchorValid || speedAnchorMs == 0 || latestLinearMs == 0)
        return false;

    const uint32_t now = millis();
    anchorAgeMs = (uint32_t)(now - speedAnchorMs);
    const uint32_t linearAgeMs = (uint32_t)(now - latestLinearMs);
    if (anchorAgeMs > 3000U || linearAgeMs > 250U)
        return false;

    speedKmph = speedEstimateKmph;
    return true;
}

#endif
