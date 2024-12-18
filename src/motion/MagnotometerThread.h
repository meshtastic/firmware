#include "PowerFSM.h"
#include "concurrency/OSThread.h"
#include "configuration.h"
#include "main.h"
#include "power.h"
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_LSM303DLH_Mag.h>

/* Assign a unique ID to this sensor at the same time */
Adafruit_LSM303DLH_Mag_Unified mag = Adafruit_LSM303DLH_Mag_Unified(12345);

#define MAG_CHECK_INTERVAL_MS 100

class MagnotometerThread : public concurrency::OSThread
{
  public:
    MagnotometerThread(ScanI2C::DeviceType type = ScanI2C::DeviceType::NONE) : OSThread("MagnotometerThread")
    {
        if (magnotometer_found.port == ScanI2C::I2CPort::NO_I2C) {
            LOG_DEBUG("MagnotometerThread disabling due to no sensors found\n");
            disable();
            return;
        }
        LOG_DEBUG("MagnotometerThread initializing\n");
        mag.enableAutoRange(true);
        mag.begin();
    }

  protected:
    int32_t runOnce() override
    {
        canSleep = true; // Assume we should not keep the board awake

        return MAG_CHECK_INTERVAL_MS;
    }
    ScanI2C::DeviceType mag_type;
};