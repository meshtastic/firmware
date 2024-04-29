#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR

#include "PowerFSM.h"
#include "concurrency/OSThread.h"
#include "main.h"
#include "power.h"

#include <Adafruit_LIS3DH.h>
#include <Adafruit_LSM6DS3TRC.h>
#include <Adafruit_MPU6050.h>
#include <Arduino.h>
#include <SensorBMA423.hpp>
#include <Wire.h>

SensorBMA423 bmaSensor;
bool BMA_IRQ = false;

#define ACCELEROMETER_CHECK_INTERVAL_MS 100
#define ACCELEROMETER_CLICK_THRESHOLD 40

int readRegister(uint8_t address, uint8_t reg, uint8_t *data, uint8_t len)
{
    Wire.beginTransmission(address);
    Wire.write(reg);
    Wire.endTransmission();
    Wire.requestFrom((uint8_t)address, (uint8_t)len);
    uint8_t i = 0;
    while (Wire.available()) {
        data[i++] = Wire.read();
    }
    return 0; // Pass
}

int writeRegister(uint8_t address, uint8_t reg, uint8_t *data, uint8_t len)
{
    Wire.beginTransmission(address);
    Wire.write(reg);
    Wire.write(data, len);
    return (0 != Wire.endTransmission());
}

namespace concurrency
{
class AccelerometerThread : public concurrency::OSThread
{
  public:
    explicit AccelerometerThread(ScanI2C::DeviceType type) : OSThread("AccelerometerThread")
    {
        if (accelerometer_found.port == ScanI2C::I2CPort::NO_I2C) {
            LOG_DEBUG("AccelerometerThread disabling due to no sensors found\n");
            disable();
            return;
        }

        if (!config.display.wake_on_tap_or_motion && !config.device.double_tap_as_button_press) {
            LOG_DEBUG("AccelerometerThread disabling due to no interested configurations\n");
            disable();
            return;
        }

        acceleremoter_type = type;
        LOG_DEBUG("AccelerometerThread initializing\n");

        if (acceleremoter_type == ScanI2C::DeviceType::MPU6050 && mpu.begin(accelerometer_found.address)) {
            LOG_DEBUG("MPU6050 initializing\n");
            // setup motion detection
            mpu.setHighPassFilter(MPU6050_HIGHPASS_0_63_HZ);
            mpu.setMotionDetectionThreshold(1);
            mpu.setMotionDetectionDuration(20);
            mpu.setInterruptPinLatch(true); // Keep it latched.  Will turn off when reinitialized.
            mpu.setInterruptPinPolarity(true);
        } else if (acceleremoter_type == ScanI2C::DeviceType::LIS3DH && lis.begin(accelerometer_found.address)) {
            LOG_DEBUG("LIS3DH initializing\n");
            lis.setRange(LIS3DH_RANGE_2_G);
            // Adjust threshold, higher numbers are less sensitive
            lis.setClick(config.device.double_tap_as_button_press ? 2 : 1, ACCELEROMETER_CLICK_THRESHOLD);
        } else if (acceleremoter_type == ScanI2C::DeviceType::BMA423 &&
                   bmaSensor.begin(accelerometer_found.address, &readRegister, &writeRegister)) {
            LOG_DEBUG("BMA423 initializing\n");
            bmaSensor.configAccelerometer(bmaSensor.RANGE_2G, bmaSensor.ODR_100HZ, bmaSensor.BW_NORMAL_AVG4,
                                          bmaSensor.PERF_CONTINUOUS_MODE);
            bmaSensor.enableAccelerometer();
            bmaSensor.configInterrupt(BMA4_LEVEL_TRIGGER, BMA4_ACTIVE_HIGH, BMA4_PUSH_PULL, BMA4_OUTPUT_ENABLE,
                                      BMA4_INPUT_DISABLE);

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
            bmaSensor.setReampAxes(bmaSensor.REMAP_TOP_LAYER_RIGHT_CORNER);
#else
            bmaSensor.setReampAxes(bmaSensor.REMAP_BOTTOM_LAYER_BOTTOM_LEFT_CORNER);
#endif
            // bmaSensor.enableFeature(bmaSensor.FEATURE_STEP_CNTR, true);
            bmaSensor.enableFeature(bmaSensor.FEATURE_TILT, true);
            bmaSensor.enableFeature(bmaSensor.FEATURE_WAKEUP, true);
            // bmaSensor.resetPedometer();

            // Turn on feature interrupt
            bmaSensor.enablePedometerIRQ();
            bmaSensor.enableTiltIRQ();
            // It corresponds to isDoubleClick interrupt
            bmaSensor.enableWakeupIRQ();
        } else if (acceleremoter_type == ScanI2C::DeviceType::LSM6DS3 && lsm.begin_I2C(accelerometer_found.address)) {
            LOG_DEBUG("LSM6DS3 initializing\n");
            // Default threshold of 2G, less sensitive options are 4, 8 or 16G
            lsm.setAccelRange(LSM6DS_ACCEL_RANGE_2_G);
#ifndef LSM6DS3_WAKE_THRESH
#define LSM6DS3_WAKE_THRESH 20
#endif
            lsm.enableWakeup(config.display.wake_on_tap_or_motion, 1, LSM6DS3_WAKE_THRESH);
            // Duration is number of occurances needed to trigger, higher threshold is less sensitive
        }
    }

  protected:
    int32_t runOnce() override
    {
        canSleep = true; // Assume we should not keep the board awake

        if (acceleremoter_type == ScanI2C::DeviceType::MPU6050 && mpu.getMotionInterruptStatus()) {
            wakeScreen();
        } else if (acceleremoter_type == ScanI2C::DeviceType::LIS3DH && lis.getClick() > 0) {
            uint8_t click = lis.getClick();
            if (!config.device.double_tap_as_button_press) {
                wakeScreen();
            }

            if (config.device.double_tap_as_button_press && (click & 0x20)) {
                buttonPress();
                return 500;
            }
        } else if (acceleremoter_type == ScanI2C::DeviceType::BMA423 && bmaSensor.readIrqStatus() != DEV_WIRE_NONE) {
            if (bmaSensor.isTilt() || bmaSensor.isDoubleTap()) {
                wakeScreen();
                return 500;
            }
        } else if (acceleremoter_type == ScanI2C::DeviceType::LSM6DS3 && lsm.shake()) {
            wakeScreen();
            return 500;
        }

        return ACCELEROMETER_CHECK_INTERVAL_MS;
    }

  private:
    void wakeScreen()
    {
        if (powerFSM.getState() == &stateDARK) {
            LOG_INFO("Tap or motion detected. Turning on screen\n");
            powerFSM.trigger(EVENT_INPUT);
        }
    }

    void buttonPress()
    {
        LOG_DEBUG("Double-tap detected. Firing button press\n");
        powerFSM.trigger(EVENT_PRESS);
    }

    ScanI2C::DeviceType acceleremoter_type;
    Adafruit_MPU6050 mpu;
    Adafruit_LIS3DH lis;
    Adafruit_LSM6DS3TRC lsm;
};

} // namespace concurrency

#endif