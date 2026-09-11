#include "BHI260APSensor.h"

#if !defined(ARCH_STM32WL) && !MESHTASTIC_EXCLUDE_I2C && defined(HAS_BHI260AP) && __has_include(<SensorBHI260AP.hpp>)
#define BOSCH_BHI260_KLIO

#include "mesh/Throttle.h"
#include <BoschFirmware.h>

#ifdef BHI260AP_INT
static volatile bool BHI_IRQ = false;
#endif

BHI260APSensor::BHI260APSensor(ScanI2C::FoundDevice foundDevice) : MotionSensor::MotionSensor(foundDevice) {}

void BHI260APSensor::onWristTilt(uint8_t, const uint8_t *, uint32_t, uint64_t *, void *user_data)
{
    static_cast<BHI260APSensor *>(user_data)->wakeRequested = true;
}
// https://github.com/lewisxhe/SensorLib/blob/master/examples/Sensors/IMU/BHI260AP_InterruptSettings/BHI260AP_InterruptSettings.ino

bool BHI260APSensor::init()
{
    LOG_WARN("Initializing BHI260AP sensor %u", deviceAddress());
    sensor.setFirmware(bosch_firmware_image, bosch_firmware_size, bosch_firmware_type);
    sensor.setBootFromFlash(bosch_firmware_type);
    if (sensor.begin(Wire, deviceAddress())) {
        sensor.setRemapAxes(SensorRemap::TOP_LAYER_BOTTOM_RIGHT_CORNER);
        BoschSensorInfo info = sensor.getSensorInfo();

        LOG_INFO("Product ID     : %02x\n", info.getProductId());
        LOG_INFO("Kernel version : %04u\n", info.getKernelVersion());
        LOG_INFO("User version   : %04u\n", info.getUserVersion());
        LOG_INFO("ROM version    : %04u\n", info.getRomVersion());
        LOG_INFO("Power state    : %s\n", (info.getHostStatus() & BHY2_HST_POWER_STATE) ? "sleeping" : "active");
        LOG_INFO("Host interface : %s\n", (info.getHostStatus() & BHY2_HST_HOST_PROTOCOL) ? "SPI" : "I2C");
        LOG_INFO("Feature status : 0x%02x\n", info.getFeatStatus());

        stepCounter = new SensorStepCounter(sensor);
        // stepDetector = new SensorStepDetector(sensor);

        // sensor.configAccelerometer(sensor.RANGE_2G, sensor.ODR_100HZ, sensor.BW_NORMAL_AVG4, sensor.PERF_CONTINUOUS_MODE);
        // sensor.enableAccelerometer();

#ifdef BHI260AP_INT
        // Defaults: active-high, level-triggered, push-pull, FIFO sources unmasked.
        InterruptConfig intConfig;
        sensor.configureInterrupt(intConfig);
        pinMode(BHI260AP_INT, INPUT);
        attachInterrupt(
            BHI260AP_INT, [] { BHI_IRQ = true; }, RISING);
#endif

        // Wrist tilt wakes the screen. Not every firmware image ships it and SensorAnyMotion
        // is BHI360-only, so fall back to step counting alone.
        constexpr uint8_t wristTilt = static_cast<uint8_t>(BoschSensorID::WRIST_TILT_GESTURE);
        if (sensor.onResultEvent(wristTilt, onWristTilt, this) && sensor.configure(wristTilt, 1.0f, 0)) {
            LOG_DEBUG("BHI260AP wrist tilt wake enabled");
        } else {
            LOG_WARN("BHI260AP firmware has no wrist tilt gesture, motion wake unavailable");
        }

        // stepDetector->enable(1.0, 0);
        stepCounter->enable(1.0, 0);
        LOG_DEBUG("BHI260AP init ok");
        return true;
    }
    LOG_DEBUG("BHI260AP init failed");
    return false;
}

int32_t BHI260APSensor::runOnce()
{
#ifdef BHI260AP_INT
    // The INT line is the fast path; the keepalive keeps the step counter alive without it.
    if (!BHI_IRQ && !Throttle::hasElapsed(lastPollMs, MOTION_SENSOR_IRQ_KEEPALIVE_MS))
        return MOTION_SENSOR_CHECK_INTERVAL_MS;
    BHI_IRQ = false;
    lastPollMs = millis();
#endif

    sensor.update();
    if (stepCounter->hasUpdated()) {
        steps = stepCounter->getStepCount();
        LOG_WARN("Step count updated: %u", steps);
        if (screen)
            screen->steps = steps;
    }
    if (wakeRequested) {
        wakeRequested = false;
        wakeScreen();
    }
#ifdef BHI260AP_INT
    // Tick fast for tilt latency; without the INT line every tick would be an I2C drain.
    return MOTION_SENSOR_CHECK_INTERVAL_MS;
#else
    return 1000;
#endif
}

#endif
