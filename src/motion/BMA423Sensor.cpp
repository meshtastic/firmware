#include "BMA423Sensor.h"

#if !defined(ARCH_PORTDUINO) && !defined(ARCH_STM32WL) && !MESHTASTIC_EXCLUDE_I2C

using namespace MotionSensorI2C;

BMA423Sensor::BMA423Sensor(ScanI2C::FoundDevice foundDevice) : MotionSensor::MotionSensor(foundDevice) {}

bool BMA423Sensor::init()
{
    if (sensor.begin(deviceAddress(), &MotionSensorI2C::readRegister, &MotionSensorI2C::writeRegister)) {
        sensor.configAccelerometer(sensor.RANGE_2G, sensor.ODR_100HZ, sensor.BW_NORMAL_AVG4, sensor.PERF_CONTINUOUS_MODE);
        sensor.enableAccelerometer();
        sensor.configInterrupt(BMA4_LEVEL_TRIGGER, BMA4_ACTIVE_HIGH, BMA4_PUSH_PULL, BMA4_OUTPUT_ENABLE, BMA4_INPUT_DISABLE);

#ifdef BMA423_INT
        pinMode(BMA4XX_INT, INPUT);
        attachInterrupt(
            BMA4XX_INT,
            [] {
                // Set interrupt to set irq value to true
                BMA_IRQ = true;
            },
            RISING); // Select the interrupt mode according to the actual circuit
#endif

#ifdef T_WATCH_S3
        // Need to raise the wrist function, need to set the correct axis
        sensor.setReampAxes(sensor.REMAP_TOP_LAYER_RIGHT_CORNER);
#else
        sensor.setReampAxes(sensor.REMAP_BOTTOM_LAYER_BOTTOM_LEFT_CORNER);
#endif
        // sensor.enableFeature(sensor.FEATURE_STEP_CNTR, true);
        sensor.enableFeature(sensor.FEATURE_TILT, true);
        sensor.enableFeature(sensor.FEATURE_WAKEUP, true);
        // sensor.resetPedometer();

        // Turn on feature interrupt
        sensor.enablePedometerIRQ();
        sensor.enableTiltIRQ();

        // It corresponds to isDoubleClick interrupt
        sensor.enableWakeupIRQ();
        LOG_DEBUG("BMA423Sensor::init ok\n");
        return true;
    }
    LOG_DEBUG("BMA423Sensor::init failed\n");
    return false;
}

int32_t BMA423Sensor::runOnce()
{
    if (sensor.readIrqStatus() != DEV_WIRE_NONE) {
        if (sensor.isTilt() || sensor.isDoubleTap()) {
            wakeScreen();
            return 500;
        }
    }
    return MOTION_SENSOR_CHECK_INTERVAL_MS;
}

#endif