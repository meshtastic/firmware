#include "PowerFSM.h"
#include "concurrency/OSThread.h"
#include "configuration.h"
#include "main.h"
#include "power.h"

#include <Adafruit_LIS3DH.h>
#include <Adafruit_MPU6050.h>
#include <Arduino.h>
#include <Wire.h>
#include <bma.h>

BMA423 bmaSensor;
bool BMA_IRQ = false;

#define ACCELEROMETER_CHECK_INTERVAL_MS 100
#define ACCELEROMETER_CLICK_THRESHOLD 40

uint16_t readRegister(uint8_t address, uint8_t reg, uint8_t *data, uint16_t len)
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

uint16_t writeRegister(uint8_t address, uint8_t reg, uint8_t *data, uint16_t len)
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
        } else if (acceleremoter_type == ScanI2C::DeviceType::BMA423 && bmaSensor.begin(readRegister, writeRegister, delay)) {
            LOG_DEBUG("BMA423 initializing\n");
            Acfg cfg;
            cfg.odr = BMA4_OUTPUT_DATA_RATE_100HZ;
            cfg.range = BMA4_ACCEL_RANGE_2G;
            cfg.bandwidth = BMA4_ACCEL_NORMAL_AVG4;
            cfg.perf_mode = BMA4_CONTINUOUS_MODE;
            bmaSensor.setAccelConfig(cfg);
            bmaSensor.enableAccel();

            struct bma4_int_pin_config pin_config;
            pin_config.edge_ctrl = BMA4_LEVEL_TRIGGER;
            pin_config.lvl = BMA4_ACTIVE_HIGH;
            pin_config.od = BMA4_PUSH_PULL;
            pin_config.output_en = BMA4_OUTPUT_ENABLE;
            pin_config.input_en = BMA4_INPUT_DISABLE;
            // The correct trigger interrupt needs to be configured as needed
            bmaSensor.setINTPinConfig(pin_config, BMA4_INTR1_MAP);

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

            struct bma423_axes_remap remap_data;
            remap_data.x_axis = 0;
            remap_data.x_axis_sign = 1;
            remap_data.y_axis = 1;
            remap_data.y_axis_sign = 0;
            remap_data.z_axis = 2;
            remap_data.z_axis_sign = 1;
            // Need to raise the wrist function, need to set the correct axis
            bmaSensor.setRemapAxes(&remap_data);
            // sensor.enableFeature(BMA423_STEP_CNTR, true);
            bmaSensor.enableFeature(BMA423_TILT, true);
            bmaSensor.enableFeature(BMA423_WAKEUP, true);
            // sensor.resetStepCounter();

            // Turn on feature interrupt
            bmaSensor.enableStepCountInterrupt();
            bmaSensor.enableTiltInterrupt();
            // It corresponds to isDoubleClick interrupt
            bmaSensor.enableWakeupInterrupt();
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
        } else if (acceleremoter_type == ScanI2C::DeviceType::BMA423 && bmaSensor.getINT()) {
            if (bmaSensor.isTilt() || bmaSensor.isDoubleClick()) {
                wakeScreen();
                return 500;
            }
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
};

} // namespace concurrency