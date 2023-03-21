#include "PowerFSM.h"
#include "concurrency/OSThread.h"
#include "configuration.h"
#include "main.h"
#include "power.h"
#include <Adafruit_MPU6050.h>

namespace concurrency
{
class AccelerometerThread : public concurrency::OSThread
{
    Adafruit_MPU6050 mpu;

  public:
    // callback returns the period for the next callback invocation (or 0 if we should no longer be called)
    AccelerometerThread(ScanI2C::DeviceType type) : OSThread("AccelerometerThread")
    {
        LOG_DEBUG("AccelerometerThread initializing\n");
        // if (accelerometer_found.port == ScanI2C::I2CPort::NO_I2C)
        // {
        //     disable();
        // }

        if (type == ScanI2C::DeviceType::MPU6050 && mpu.begin(accelerometer_found.address)) {
            LOG_DEBUG("MPU6050 initializing\n");
            // setup motion detection
            mpu.setHighPassFilter(MPU6050_HIGHPASS_0_63_HZ);
            mpu.setMotionDetectionThreshold(1);
            mpu.setMotionDetectionDuration(20);
            mpu.setInterruptPinLatch(true); // Keep it latched.  Will turn off when reinitialized.
            mpu.setInterruptPinPolarity(true);
        }
    }

  protected:
    /// If the button is pressed we suppress CPU sleep until release
    int32_t runOnce() override
    {
        canSleep = true; // Assume we should not keep the board awake
        LOG_DEBUG("AccelerometerThread runOnce()\n");

        if (mpu.getMotionInterruptStatus()) {
            LOG_DEBUG("Motion detected, turning on screen\n");
            screen->setOn(true);
        }
        return 10;
    }
};

} // namespace concurrency
