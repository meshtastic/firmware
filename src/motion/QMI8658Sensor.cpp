#include "QMI8658Sensor.h"

#if !defined(ARCH_STM32WL) && !MESHTASTIC_EXCLUDE_I2C && __has_include(<SensorQMI8658.hpp>)

#include "NodeDB.h"
#include "detect/ScanI2CTwoWire.h"
#include <math.h>

// Accelerometer configuration. 2G full-scale gives the best gravity resolution for the tilt
// compensation that the (future) separate compass module will apply to these samples.
static constexpr SensorQMI8658::AccelRange QMI8658_ACCEL_RANGE = SensorQMI8658::ACC_RANGE_2G;
static constexpr SensorQMI8658::AccelODR QMI8658_ACCEL_ODR = SensorQMI8658::ACC_ODR_125Hz;

// Any-motion slope threshold (in mg) used to wake the screen. Tunable: raise to reduce false wakes,
// lower to make it more sensitive. 200mg (~0.2g) requires a deliberate movement.
static constexpr float QMI8658_ANY_MOTION_THRESHOLD_MG = 200.0f;
static constexpr uint8_t QMI8658_ANY_MOTION_WINDOW = 1;

// Optional board-defined rotation (degrees) applied to the accel X/Y before publishing to the compass
// fusion path, mirroring the ICM42607P driver. Defaults to no rotation.
static constexpr float QMI8658_ACCEL_TO_COMPASS_ROTATION_DEG_VALUE =
#ifdef QMI8658_ACCEL_TO_COMPASS_ROTATION_DEG
    QMI8658_ACCEL_TO_COMPASS_ROTATION_DEG;
#else
    0.0f;
#endif

QMI8658Sensor::QMI8658Sensor(ScanI2C::FoundDevice foundDevice) : MotionSensor::MotionSensor(foundDevice) {}

bool QMI8658Sensor::init()
{
    LOG_DEBUG("QMI8658 begin on addr 0x%02X (port=%d)", deviceAddress(), devicePort());
    TwoWire *wire = ScanI2CTwoWire::fetchI2CBus(device.address);

    if (!sensor.begin(*wire, deviceAddress())) {
        LOG_DEBUG("QMI8658 init failed");
        return false;
    }

    sensor.configAccelerometer(QMI8658_ACCEL_RANGE, QMI8658_ACCEL_ODR, SensorQMI8658::LPF_MODE_0);
    sensor.enableAccelerometer();

    // Configure the on-chip any-motion engine so we can wake the screen without a dedicated interrupt pin.
    // configMotion() runs alongside normal accel data output (unlike Wake-on-Motion, which halts data), so
    // we keep publishing samples for compass fusion while still detecting motion.
    wakeOnMotion = config.display.wake_on_tap_or_motion;
    if (wakeOnMotion) {
        const uint8_t modeCtrl = SensorQMI8658::ANY_MOTION_EN_X | SensorQMI8658::ANY_MOTION_EN_Y | SensorQMI8658::ANY_MOTION_EN_Z;
        // No-motion detection is left disabled (unreliable per the SensorLib example); its thresholds/windows
        // are still required arguments but are ignored when the mode bits above are clear.
        sensor.configMotion(modeCtrl, QMI8658_ANY_MOTION_THRESHOLD_MG, QMI8658_ANY_MOTION_THRESHOLD_MG,
                            QMI8658_ANY_MOTION_THRESHOLD_MG, QMI8658_ANY_MOTION_WINDOW, /*NoMotion X/Y/Z*/ 0.1f, 0.1f, 0.1f,
                            /*NoMotionWindow*/ 1, /*SigMotionWaitWindow*/ 1, /*SigMotionConfirmWindow*/ 1);
        sensor.enableMotionDetect();
    }

    LOG_DEBUG("QMI8658 init ok");
    return true;
}

int32_t QMI8658Sensor::runOnce()
{
    float ax, ay, az;
    if (sensor.getAccelerometer(ax, ay, az)) {
        if (QMI8658_ACCEL_TO_COMPASS_ROTATION_DEG_VALUE != 0.0f) {
            static const float rotRad = QMI8658_ACCEL_TO_COMPASS_ROTATION_DEG_VALUE * DEG_TO_RAD;
            static const float cosTheta = cosf(rotRad);
            static const float sinTheta = sinf(rotRad);
            const float rotatedX = (ax * cosTheta) - (ay * sinTheta);
            const float rotatedY = (ax * sinTheta) + (ay * cosTheta);
            ax = rotatedX;
            ay = rotatedY;
        }

        // Match the accel sign convention used by the other FusionCompass sensor paths (e.g. ICM42607P).
        // The final handedness must be verified against the QMI8658 datasheet and real calibration once the
        // separate compass module is wired up; do not hand-tune the signs before then.
        publishCompassAccelSample(ax, ay, az);
    }

    if (wakeOnMotion && (sensor.getStatusRegister() & SensorQMI8658::EVENT_ANY_MOTION)) {
        wakeScreen();
    }

    return MOTION_SENSOR_CHECK_INTERVAL_MS;
}

#endif
