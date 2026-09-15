#include "ICM20948Sensor.h"

#if !defined(ARCH_STM32WL) && !MESHTASTIC_EXCLUDE_I2C && __has_include(<ICM_20948.h>)
#include "concurrency/LockGuard.h"
#include "detect/ScanI2CTwoWire.h"
#include "mesh/Throttle.h"
#include <math.h>
#if !defined(MESHTASTIC_EXCLUDE_SCREEN)

// screen is defined in main.cpp
extern std::unique_ptr<graphics::Screen> screen;
#endif

namespace
{
constexpr uint32_t ICM_SPEED_BRIDGE_MAX_MS = 3000U;
constexpr uint32_t ICM_SPEED_ACCEL_MAX_AGE_MS = 250U;
constexpr float ICM_SPEED_MIN_COURSE_KMPH = 1.5f;
constexpr float ICM_SPEED_ACCEL_DEADBAND_MPS2 = 0.15f;
constexpr float ICM_SPEED_ACCEL_LIMIT_MPS2 = 4.0f;
constexpr float ICM_SPEED_MAX_DIVERGENCE_KMPH = 15.0f;
constexpr float STANDARD_GRAVITY_MPS2 = 9.80665f;

struct IcmSpeedBridgeState {
    bool anchorValid = false;
    float anchorSpeedKmph = 0.0f;
    float courseDeg = 0.0f;
    float estimateKmph = 0.0f;
    float filteredForwardAccelMps2 = 0.0f;
    uint32_t anchorMs = 0;
    uint32_t integrationMs = 0;
    uint32_t accelerationMs = 0;
};

concurrency::Lock icmSpeedBridgeLock;
IcmSpeedBridgeState icmSpeedBridge;

float wrap360(float value)
{
    while (value < 0.0f)
        value += 360.0f;
    while (value >= 360.0f)
        value -= 360.0f;
    return value;
}

float wrapDelta180(float value)
{
    while (value > 180.0f)
        value -= 360.0f;
    while (value < -180.0f)
        value += 360.0f;
    return value;
}

bool isSpeedBridgeArmed(uint32_t now)
{
    concurrency::LockGuard guard(&icmSpeedBridgeLock);
    return icmSpeedBridge.anchorValid && (uint32_t)(now - icmSpeedBridge.anchorMs) <= ICM_SPEED_BRIDGE_MAX_MS;
}

void updateSpeedBridgeFromEarthAcceleration(const FusionVector &earthAcceleration, uint32_t now, bool fusionUsable)
{
    concurrency::LockGuard guard(&icmSpeedBridgeLock);

    if (!icmSpeedBridge.anchorValid)
        return;

    const uint32_t anchorAgeMs = (uint32_t)(now - icmSpeedBridge.anchorMs);
    if (anchorAgeMs > ICM_SPEED_BRIDGE_MAX_MS) {
        icmSpeedBridge.anchorValid = false;
        return;
    }

    // Never integrate the first sample after an anchor/restart, and never
    // integrate while the AHRS is still in its high-gain startup phase.
    const uint32_t dtMs = (icmSpeedBridge.integrationMs == 0U) ? 0U : (uint32_t)(now - icmSpeedBridge.integrationMs);
    icmSpeedBridge.integrationMs = now;
    if (!fusionUsable || dtMs == 0U || dtMs > 200U)
        return;

    // FusionConventionNed earth acceleration: X=North, Y=East, Z=Down.
    // Project horizontal acceleration onto the last trustworthy GNSS COG.
    const float courseRad = FusionDegreesToRadians(icmSpeedBridge.courseDeg);
    float forwardAccelMps2 =
        (earthAcceleration.axis.x * cosf(courseRad) + earthAcceleration.axis.y * sinf(courseRad)) * STANDARD_GRAVITY_MPS2;

    if (forwardAccelMps2 > ICM_SPEED_ACCEL_LIMIT_MPS2)
        forwardAccelMps2 = ICM_SPEED_ACCEL_LIMIT_MPS2;
    else if (forwardAccelMps2 < -ICM_SPEED_ACCEL_LIMIT_MPS2)
        forwardAccelMps2 = -ICM_SPEED_ACCEL_LIMIT_MPS2;

    // Mild low-pass before integration; the deadband suppresses residual
    // gravity/tilt noise so it cannot slowly walk the speed estimate.
    icmSpeedBridge.filteredForwardAccelMps2 = 0.60f * icmSpeedBridge.filteredForwardAccelMps2 + 0.40f * forwardAccelMps2;
    if (fabsf(icmSpeedBridge.filteredForwardAccelMps2) < ICM_SPEED_ACCEL_DEADBAND_MPS2)
        icmSpeedBridge.filteredForwardAccelMps2 = 0.0f;

    float estimate = icmSpeedBridge.estimateKmph + icmSpeedBridge.filteredForwardAccelMps2 * (dtMs * 0.001f) * 3.6f;
    if (estimate < 0.0f)
        estimate = 0.0f;

    const float minAllowed = icmSpeedBridge.anchorSpeedKmph > ICM_SPEED_MAX_DIVERGENCE_KMPH
                                 ? icmSpeedBridge.anchorSpeedKmph - ICM_SPEED_MAX_DIVERGENCE_KMPH
                                 : 0.0f;
    const float maxAllowed = icmSpeedBridge.anchorSpeedKmph + ICM_SPEED_MAX_DIVERGENCE_KMPH;
    if (estimate < minAllowed)
        estimate = minAllowed;
    else if (estimate > maxAllowed)
        estimate = maxAllowed;

    icmSpeedBridge.estimateKmph = estimate;
    icmSpeedBridge.accelerationMs = now;
}
} // namespace

// Flag when an interrupt has been detected
volatile static bool ICM20948_IRQ = false;

// Interrupt service routine
void ICM20948SetInterrupt()
{
    ICM20948_IRQ = true;
}

ICM20948Sensor::ICM20948Sensor(ScanI2C::FoundDevice foundDevice) : MotionSensor::MotionSensor(foundDevice) {}

bool ICM20948Sensor::init()
{
    // Initialise the sensor
    sensor = ICM20948Singleton::GetInstance();
    if (!sensor->init(device))
        return false;

    // Enable simple Wake on Motion
    const bool wakeOnMotionOk = sensor->setWakeOnMotion();
    if (!wakeOnMotionOk)
        return false;

    loadMagnetometerCalibration(compassCalibrationFileName, highestX, lowestX, highestY, lowestY, highestZ, lowestZ);

    // 20 Hz 9-DoF fusion. The calibrated magnetic compass remains the
    // absolute reference; gyro supplies the fast relative motion between
    // magnetic corrections.
    FusionAhrsInitialise(&ahrs);
    FusionAhrsSettings ahrsSettings;
    ahrsSettings.convention = FusionConventionNed;
    ahrsSettings.gain = 0.5f;
    ahrsSettings.gyroscopeRange = 250.0f;
    ahrsSettings.accelerationRejection = 20.0f;
    ahrsSettings.magneticRejection = 20.0f;
    ahrsSettings.recoveryTriggerPeriod = 100U; // ~5 s at 20 Hz
    FusionAhrsSetSettings(&ahrs, &ahrsSettings);

    FusionBiasInitialise(&gyroBias);
    FusionBiasSettings biasSettings;
    biasSettings.sampleRate = 1000.0f / MOTION_SENSOR_CHECK_INTERVAL_MS;
    biasSettings.stationaryThreshold = 3.0f;
    biasSettings.stationaryPeriod = 3.0f;
    FusionBiasSetSettings(&gyroBias, &biasSettings);

    fusionInitialised = false;
    lastFusionUpdateMs = 0;
    return true;
}

int32_t ICM20948Sensor::runOnce()
{
    const uint32_t now = millis();
    const bool speedAssistActive = isSpeedBridgeArmed(now);

#if !defined(MESHTASTIC_EXCLUDE_SCREEN) && HAS_SCREEN
    // Keep the IMU awake for at most three seconds after a trustworthy GNSS
    // speed/course anchor, even if the E-Ink screen is dark. This allows the
    // accelerometer bridge to cover a short missing GNSS speed sample.
    if (screen && !doCalibration && !speedAssistActive && !screen->isScreenOn() && !config.display.wake_on_tap_or_motion &&
        !config.device.double_tap_as_button_press) {
        if (!isAsleep) {
            LOG_DEBUG("sleeping IMU");
            sensor->sleep(true);
            isAsleep = true;
        }
        return MOTION_SENSOR_CHECK_INTERVAL_MS;
    }
#endif

    if (isAsleep) {
        sensor->sleep(false);
        isAsleep = false;
        // Do not integrate a long sleep gap as if it were one IMU sample.
        fusionInitialised = false;
        lastFusionUpdateMs = 0;
    }

    bool haveSample = false;
    float magX = 0.0f, magY = 0.0f, magZ = 0.0f;
    if (sensor->dataReady()) {
        sensor->getAGMT();
        magX = sensor->agmt.mag.axes.x;
        magY = sensor->agmt.mag.axes.y;
        magZ = sensor->agmt.mag.axes.z;
        haveSample = true;
    }

#if !defined(MESHTASTIC_EXCLUDE_SCREEN) && HAS_SCREEN
    if (doCalibration) {
        beginCalibrationDisplay(showingScreen);
        if (haveSample) {
            updateCalibrationExtrema(magX, magY, magZ, highestX, lowestX, highestY, lowestY, highestZ, lowestZ);
        }
        finishCalibrationIfExpired(showingScreen, compassCalibrationFileName, highestX, lowestX, highestY, lowestY, highestZ,
                                   lowestZ);
    }
#endif

    if (haveSample) {
        // Keep the existing magnetometer calibration and axis mapping because
        // this is the absolute heading path already proven against the phone.
        magX -= (highestX + lowestX) / 2.0f;
        magY -= (highestY + lowestY) / 2.0f;
        magZ -= (highestZ + lowestZ) / 2.0f;

        FusionVector accel;
        accel.axis.x = sensor->accX() * 0.001f; // SparkFun API returns milli-g
        accel.axis.y = -sensor->accY() * 0.001f;
        accel.axis.z = -sensor->accZ() * 0.001f;

        FusionVector gyro;
        gyro.axis.x = sensor->gyrX(); // degrees/s
        gyro.axis.y = -sensor->gyrY();
        gyro.axis.z = -sensor->gyrZ();

        FusionVector mag;
        mag.axis.x = magX;
        mag.axis.y = magY;
        mag.axis.z = magZ;

        // If we're set to one of the inverted positions, rotate all three IMU
        // vectors together. The old code already did this for accel+mag; gyro
        // must follow the same body-frame transform for correct fast turns.
        if (config.display.compass_orientation > meshtastic_Config_DisplayConfig_CompassOrientation_DEGREES_270) {
            accel = FusionRemap(accel, FusionRemapAlignmentNXNYPZ);
            gyro = FusionRemap(gyro, FusionRemapAlignmentNXNYPZ);
            mag = FusionRemap(mag, FusionRemapAlignmentNXNYPZ);
        }

        publishCompassAccelSample(accel.axis.x, accel.axis.y, accel.axis.z);
        publishCompassMagSample(mag.axis.x, mag.axis.y, mag.axis.z);

        const float magneticHeading = wrap360(FusionCompass(accel, mag, FusionConventionNed));
        const FusionVector correctedGyro = FusionBiasUpdate(&gyroBias, gyro);

        uint32_t dtMs = lastFusionUpdateMs == 0U ? 0U : (uint32_t)(now - lastFusionUpdateMs);
        lastFusionUpdateMs = now;

        // A long pause means the IMU was asleep or scheduling was delayed.
        // Restart the attitude state instead of integrating that gap.
        if (!fusionInitialised || dtMs == 0U || dtMs > 250U) {
            FusionAhrsRestart(&ahrs);
            FusionAhrsSetHeading(&ahrs, magneticHeading);
            fusionInitialised = true;
            dtMs = MOTION_SENSOR_CHECK_INTERVAL_MS;
        }

        float dtSeconds = dtMs * 0.001f;
        if (dtSeconds < 0.01f)
            dtSeconds = 0.01f;
        else if (dtSeconds > 0.20f)
            dtSeconds = 0.20f;

        // Magnetometer = absolute heading, gyro = fast prediction,
        // accelerometer = roll/pitch/gravity reference.
        FusionAhrsUpdateExternalHeading(&ahrs, correctedGyro, accel, magneticHeading, dtSeconds);

        FusionEuler euler = FusionQuaternionToEuler(FusionAhrsGetQuaternion(&ahrs));
        float fusedHeading = wrap360(euler.angle.yaw);

        // Fail safe: the gyro is never allowed to become an independent
        // long-term compass. If fusion ever separates grossly from the proven
        // magnetic reference, snap the yaw reference back to the magnetometer.
        if (fabsf(wrapDelta180(fusedHeading - magneticHeading)) > 90.0f) {
            FusionAhrsSetHeading(&ahrs, magneticHeading);
            fusedHeading = magneticHeading;
        }

#if !defined(MESHTASTIC_EXCLUDE_SCREEN) && HAS_SCREEN
        if (screen)
            screen->setHeading(applyCompassOrientation(fusedHeading));
#endif

        const FusionAhrsFlags flags = FusionAhrsGetFlags(&ahrs);
        const FusionVector earthAcceleration = FusionAhrsGetEarthAcceleration(&ahrs);
        updateSpeedBridgeFromEarthAcceleration(earthAcceleration, now, fusionInitialised && !flags.startup);
    }

#ifdef ICM_20948_INT_PIN
    if (ICM20948_IRQ) {
        ICM20948_IRQ = false;
        intPinProven = true;
        sensor->clearInterrupts();
        wakeScreen();
        return MOTION_SENSOR_CHECK_INTERVAL_MS;
    }
    // Back off to the keepalive only once the pin has actually fired. No vendor firmware
    // uses this line, so an unproven one keeps full-rate polling instead of costing latency.
    if (intPinProven && !Throttle::hasElapsed(lastWomPollMs, MOTION_SENSOR_IRQ_KEEPALIVE_MS))
        return MOTION_SENSOR_CHECK_INTERVAL_MS;
    lastWomPollMs = millis();
#endif

    auto status = sensor->setBank(0);
    if (sensor->status != ICM_20948_Stat_Ok) {
        LOG_DEBUG("ICM20948 isWakeOnMotion failed to set bank - %s", sensor->statusString());
        return MOTION_SENSOR_CHECK_INTERVAL_MS;
    }

    ICM_20948_INT_STATUS_t int_stat;
    status = sensor->read(AGB0_REG_INT_STATUS, (uint8_t *)&int_stat, sizeof(ICM_20948_INT_STATUS_t));
    if (status != ICM_20948_Stat_Ok) {
        LOG_DEBUG("ICM20948 isWakeOnMotion failed to read interrupts - %s", sensor->statusString());
        return MOTION_SENSOR_CHECK_INTERVAL_MS;
    }

    if (int_stat.WOM_INT != 0) {
        // Wake up!
        wakeScreen();
    }
    return MOTION_SENSOR_CHECK_INTERVAL_MS;
}

void ICM20948Sensor::setGnssMotionAnchor(float speedKmph, float courseDeg)
{
    if (!(speedKmph >= ICM_SPEED_MIN_COURSE_KMPH) || !(courseDeg >= 0.0f && courseDeg < 360.0f)) {
        invalidateGnssMotionAnchor();
        return;
    }

    concurrency::LockGuard guard(&icmSpeedBridgeLock);
    icmSpeedBridge.anchorValid = true;
    icmSpeedBridge.anchorSpeedKmph = speedKmph;
    icmSpeedBridge.courseDeg = courseDeg;
    icmSpeedBridge.estimateKmph = speedKmph;
    icmSpeedBridge.filteredForwardAccelMps2 = 0.0f;
    icmSpeedBridge.anchorMs = millis();
    icmSpeedBridge.integrationMs = 0;
    icmSpeedBridge.accelerationMs = 0;
}

void ICM20948Sensor::invalidateGnssMotionAnchor()
{
    concurrency::LockGuard guard(&icmSpeedBridgeLock);
    icmSpeedBridge.anchorValid = false;
    icmSpeedBridge.integrationMs = 0;
    icmSpeedBridge.accelerationMs = 0;
}

bool ICM20948Sensor::getBridgedSpeedKmph(float &speedKmph, uint32_t &anchorAgeMs)
{
    concurrency::LockGuard guard(&icmSpeedBridgeLock);

    if (!icmSpeedBridge.anchorValid)
        return false;

    const uint32_t now = millis();
    anchorAgeMs = (uint32_t)(now - icmSpeedBridge.anchorMs);
    if (anchorAgeMs > ICM_SPEED_BRIDGE_MAX_MS) {
        icmSpeedBridge.anchorValid = false;
        return false;
    }

    if (icmSpeedBridge.accelerationMs == 0U || (uint32_t)(now - icmSpeedBridge.accelerationMs) > ICM_SPEED_ACCEL_MAX_AGE_MS)
        return false;

    speedKmph = icmSpeedBridge.estimateKmph;
    return true;
}

void ICM20948Sensor::calibrate(uint16_t forSeconds)
{
#if !defined(MESHTASTIC_EXCLUDE_SCREEN) && HAS_SCREEN
    LOG_DEBUG("ICM20948 cal start %is", forSeconds);
    if (sensor->dataReady()) {
        sensor->getAGMT();
        seedCalibrationExtrema(sensor->agmt.mag.axes.x, sensor->agmt.mag.axes.y, sensor->agmt.mag.axes.z, highestX, lowestX,
                               highestY, lowestY, highestZ, lowestZ);
    } else {
        seedCalibrationExtrema(0.0f, 0.0f, 0.0f, highestX, lowestX, highestY, lowestY, highestZ, lowestZ);
    }

    startCalibrationWindow(forSeconds);
#endif
}
// ----------------------------------------------------------------------
// ICM20948Singleton
// ----------------------------------------------------------------------

// Get a singleton wrapper for an Sparkfun ICM_20948_I2C
ICM20948Singleton *ICM20948Singleton::GetInstance()
{
    if (pinstance == nullptr) {
        pinstance = new ICM20948Singleton();
    }
    return pinstance;
}

ICM20948Singleton::ICM20948Singleton() {}

ICM20948Singleton::~ICM20948Singleton() {}

ICM20948Singleton *ICM20948Singleton::pinstance{nullptr};

// Initialise the ICM20948 Sensor
bool ICM20948Singleton::init(ScanI2C::FoundDevice device)
{
#ifdef ICM_20948_DEBUG
    // Set ICM_20948_DEBUG to enable helpful debug messages on Serial
    enableDebugging();
#endif

    // startup; the bus is resolved via the scanner: WIRE1 may be a bridged
    // bus rather than the local Wire1 (e.g. SenseCAP Indicator)
    TwoWire &bus = *ScanI2CTwoWire::fetchI2CBus(device.address);

    bool bAddr = (device.address.address == 0x69);
    delay(100);

    LOG_DEBUG("ICM20948 begin on addr 0x%02X (port=%d, bAddr=%d)", device.address.address, device.address.port, bAddr);

    ICM_20948_Status_e status = begin(bus, bAddr);
    if (status != ICM_20948_Stat_Ok) {
        LOG_DEBUG("ICM20948 init begin - %s", statusString());
        return false;
    }

    // SW reset to make sure the device starts in a known state
    if (swReset() != ICM_20948_Stat_Ok) {
        LOG_DEBUG("ICM20948 init reset - %s", statusString());
        return false;
    }
    delay(200);

    // Now wake the sensor up
    if (sleep(false) != ICM_20948_Stat_Ok) {
        LOG_DEBUG("ICM20948 init wake - %s", statusString());
        return false;
    }

    if (lowPower(false) != ICM_20948_Stat_Ok) {
        LOG_DEBUG("ICM20948 init high power - %s", statusString());
        return false;
    }

    if (startupMagnetometer(false) != ICM_20948_Stat_Ok) {
        LOG_DEBUG("ICM20948 init magnetometer - %s", statusString());
        return false;
    }

#ifdef ICM_20948_INT_PIN

    // Active low
    cfgIntActiveLow(true);
    LOG_DEBUG("ICM20948 init set cfgIntActiveLow - %s", statusString());

    // Push-pull
    cfgIntOpenDrain(false);
    LOG_DEBUG("ICM20948 init set cfgIntOpenDrain - %s", statusString());

    // If enabled, *ANY* read will clear the INT_STATUS register.
    cfgIntAnyReadToClear(true);
    LOG_DEBUG("ICM20948 init set cfgIntAnyReadToClear - %s", statusString());

    // Latch the interrupt until cleared
    cfgIntLatch(true);
    LOG_DEBUG("ICM20948 init set cfgIntLatch - %s", statusString());

    // Set up an interrupt pin with an internal pullup for active low
    pinMode(ICM_20948_INT_PIN, INPUT_PULLUP);

    // Set up an interrupt service routine
    attachInterrupt(ICM_20948_INT_PIN, ICM20948SetInterrupt, FALLING);

#endif
    return true;
}

#ifdef ICM_20948_DMP_IS_ENABLED

// Stub
bool ICM20948Sensor::initDMP()
{
    return false;
}

#endif

bool ICM20948Singleton::setWakeOnMotion()
{
    // Set WoM threshold in milli G's
    auto status = WOMThreshold(ICM_20948_WOM_THRESHOLD);
    if (status != ICM_20948_Stat_Ok)
        return false;

    // Enable WoM Logic mode 1 = Compare the current sample with the previous sample
    status = WOMLogic(true, 1);
    LOG_DEBUG("ICM20948 init set WOMLogic - %s", statusString());
    if (status != ICM_20948_Stat_Ok)
        return false;

    // Enable interrupts on WakeOnMotion
    status = intEnableWOM(true);
    LOG_DEBUG("ICM20948 init set intEnableWOM - %s", statusString());
    return status == ICM_20948_Stat_Ok;
}

#endif
