#include "PowerFSM.h"
#include "concurrency/OSThread.h"
#include "configuration.h"
#include "main.h"
#include "power.h"

#include <Adafruit_LIS3DH.h>
#include <Adafruit_MPU6050.h>

#define ACCELEROMETER_CHECK_INTERVAL_MS 100

namespace concurrency
{
class AccelerometerThread : public concurrency::OSThread
{
  public:
    AccelerometerThread(ScanI2C::DeviceType type = ScanI2C::DeviceType::NONE) : OSThread("AccelerometerThread")
    {
        if (accelerometer_found.port == ScanI2C::I2CPort::NO_I2C || (!config.display.wake_on_tap_or_motion && !config.device.double_tap_as_button_press)) {
            disable();
            return;
        }
        accleremoter_type = type;
        LOG_DEBUG("AccelerometerThread initializing\n");
        
        if (accleremoter_type == ScanI2C::DeviceType::MPU6050 && mpu.begin(accelerometer_found.address)) {
            LOG_DEBUG("MPU6050 initializing\n");
            // setup motion detection
            mpu.setHighPassFilter(MPU6050_HIGHPASS_0_63_HZ);
            mpu.setMotionDetectionThreshold(1);
            mpu.setMotionDetectionDuration(20);
            mpu.setInterruptPinLatch(true); // Keep it latched.  Will turn off when reinitialized.
            mpu.setInterruptPinPolarity(true);
        } else if (accleremoter_type == ScanI2C::DeviceType::LIS3DH && lis.begin(accelerometer_found.address)) {
            LOG_DEBUG("LIS3DH initializing\n");
            lis.setRange(LIS3DH_RANGE_2_G);

            const uint8_t numberOfTaps = config.device.double_tap_as_button_press ? 2 : 1;
            // Adjust threshhold, higher numbers are less sensitive
            lis.setClick(numberOfTaps, 40);
        }
    }

  protected:
    int32_t runOnce() override
    {
        canSleep = true; // Assume we should not keep the board awake

        if (accleremoter_type == ScanI2C::DeviceType::MPU6050 && mpu.getMotionInterruptStatus()) {
            wakeScreen();
        } else if (accleremoter_type == ScanI2C::DeviceType::LIS3DH && lis.getClick() != 0) {
            uint8_t click = lis.getClick();
            LOG_DEBUG("click=%x\n", click);
            if (!(click & 0x30)) {
                return ACCELEROMETER_CHECK_INTERVAL_MS;
            }

            if (!config.device.double_tap_as_button_press && (click & 0x10)) {
                wakeScreen();
            }
            else if (config.device.double_tap_as_button_press && (click & 0x20)) {
                buttonPress();
            }
        }
        return ACCELEROMETER_CHECK_INTERVAL_MS;
    }

  private:
    void wakeScreen()
    {
        LOG_DEBUG("Tap or motion detected. Turning on screen\n");
        if (powerFSM.getState() == &stateDARK) {
            powerFSM.trigger(EVENT_INPUT);
        }
    }

    void buttonPress()
    {
        LOG_DEBUG("Double-tap detected. Firing button press\n");
        powerFSM.trigger(EVENT_PRESS);
    }

    ScanI2C::DeviceType accleremoter_type;
    Adafruit_MPU6050 mpu;
    Adafruit_LIS3DH lis;
};

} // namespace concurrency
