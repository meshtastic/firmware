#include "BMA423Sensor.h"

#if !defined(ARCH_STM32WL) && !MESHTASTIC_EXCLUDE_I2C && defined(HAS_BMA423) && __has_include(<SensorBMA423.hpp>)

#include "mesh/Throttle.h"

#ifdef BMA4XX_INT
static volatile bool BMA_IRQ = false;
#endif

BMA423Sensor::BMA423Sensor(ScanI2C::FoundDevice foundDevice) : MotionSensor::MotionSensor(foundDevice) {}

bool BMA423Sensor::init()
{
    if (!sensor.begin(Wire, deviceAddress())) {
        LOG_DEBUG("BMA423 init failed");
        return false;
    }

    if (!sensor.configAccelerometer(OperationMode::NORMAL, AccelFullScaleRange::FS_2G, 100.0f, AccelBandwidth::NORMAL_AVG4,
                                    AccelPerfMode::CONTINUOUS_MODE)) {
        LOG_DEBUG("BMA423 accelerometer config failed");
        return false;
    }

#ifdef T_WATCH_S3
    // Need to raise the wrist function, need to set the correct axis
    sensor.setRemapAxes(SensorRemap::TOP_LAYER_RIGHT_CORNER);
#else
    sensor.setRemapAxes(SensorRemap::BOTTOM_LAYER_BOTTOM_LEFT_CORNER);
#endif

#ifdef BMA4XX_INT
    // enableTiltDetector()/enableTapDetector() only map the feature onto INT1, and the BMA4
    // reset default leaves that pin's output driver off. Arm it push-pull active-high.
    if (!sensor.setInterruptPinConfig(InterruptPinMap::PIN1, false, false, true, false))
        LOG_DEBUG("BMA423 INT1 pin config failed, keeping the polled path"); // not fatal
#endif

    // The tap detector defaults to double tap; tilt and double tap both wake the screen.
    sensor.setOnTiltDetectedCallback([this] { wakeRequested = true; });
    sensor.setOnTapCallback([this](TapType) { wakeRequested = true; });
    if (!sensor.enableTiltDetector(true, true) || !sensor.enableTapDetector(true, true)) {
        LOG_DEBUG("BMA423 wake detector setup failed");
        return false;
    }

#ifdef BMA4XX_INT
    pinMode(BMA4XX_INT, INPUT);
    attachInterrupt(
        BMA4XX_INT, [] { BMA_IRQ = true; }, RISING);
#endif

    LOG_DEBUG("BMA423 init ok");
    return true;
}

int32_t BMA423Sensor::runOnce()
{
#ifdef BMA4XX_INT
    // INT1 is the fast path; update() reads and clears the status register and fires the callbacks.
    if (!BMA_IRQ && !Throttle::hasElapsed(lastPollMs, MOTION_SENSOR_IRQ_KEEPALIVE_MS))
        return MOTION_SENSOR_CHECK_INTERVAL_MS;
    BMA_IRQ = false;
    lastPollMs = millis();
#endif

    wakeRequested = false;
    sensor.update();
    if (wakeRequested) {
        wakeScreen();
        return 500;
    }
    return MOTION_SENSOR_CHECK_INTERVAL_MS;
}

#endif
