#include "BMA423Sensor.h"

#if !defined(ARCH_STM32WL) && !MESHTASTIC_EXCLUDE_I2C && defined(HAS_BMA423) && __has_include(<SensorBMA423.hpp>)

BMA423Sensor::BMA423Sensor(ScanI2C::FoundDevice foundDevice) : MotionSensor::MotionSensor(foundDevice) {}

bool BMA423Sensor::init()
{
    if (!sensor.begin(Wire, deviceAddress())) {
        LOG_DEBUG("BMA423 init failed");
        return false;
    }

    sensor.configAccelerometer(OperationMode::NORMAL, AccelFullScaleRange::FS_2G, 100.0f, AccelBandwidth::NORMAL_AVG4,
                               AccelPerfMode::CONTINUOUS_MODE);

#ifdef T_WATCH_S3
    // Need to raise the wrist function, need to set the correct axis
    sensor.setRemapAxes(SensorRemap::TOP_LAYER_RIGHT_CORNER);
#else
    sensor.setRemapAxes(SensorRemap::BOTTOM_LAYER_BOTTOM_LEFT_CORNER);
#endif

    // The tap detector defaults to double tap; tilt and double tap both wake the screen.
    sensor.setOnTiltDetectedCallback([this] { wakeRequested = true; });
    sensor.setOnTapCallback([this](TapType) { wakeRequested = true; });
    sensor.enableTiltDetector(true, true);
    sensor.enableTapDetector(true, true);

    LOG_DEBUG("BMA423 init ok");
    return true;
}

int32_t BMA423Sensor::runOnce()
{
    wakeRequested = false;
    sensor.update();
    if (wakeRequested) {
        wakeScreen();
        return 500;
    }
    return MOTION_SENSOR_CHECK_INTERVAL_MS;
}

#endif
