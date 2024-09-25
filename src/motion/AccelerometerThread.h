#pragma once
#ifndef _ACCELEROMETER_H_
#define _ACCELEROMETER_H_

#include "configuration.h"

#if !defined(ARCH_PORTDUINO) && !defined(ARCH_STM32WL) && !MESHTASTIC_EXCLUDE_I2C

#include "../concurrency/OSThread.h"
#include "BMA423Sensor.h"
#include "BMX160Sensor.h"
#include "ICM20948Sensor.h"
#include "LIS3DHSensor.h"
#include "LSM6DS3Sensor.h"
#include "MPU6050Sensor.h"
#include "MotionSensor.h"
#include "STK8XXXSensor.h"

extern ScanI2C::DeviceAddress accelerometer_found;

class AccelerometerThread : public concurrency::OSThread
{
  private:
    MotionSensor *sensor = nullptr;
    bool isInitialised = false;

  public:
    explicit AccelerometerThread(ScanI2C::FoundDevice foundDevice) : OSThread("AccelerometerThread")
    {
        device = foundDevice;
        init();
    }

    explicit AccelerometerThread(ScanI2C::DeviceType type) : AccelerometerThread(ScanI2C::FoundDevice{type, accelerometer_found})
    {
    }

    void start()
    {
        init();
        setIntervalFromNow(0);
    };

  protected:
    int32_t runOnce() override
    {
        // Assume we should not keep the board awake
        canSleep = true;

        if (isInitialised)
            return sensor->runOnce();

        return MOTION_SENSOR_CHECK_INTERVAL_MS;
    }

  private:
    ScanI2C::FoundDevice device;

    void init()
    {
        if (isInitialised)
            return;

        if (device.address.port == ScanI2C::I2CPort::NO_I2C || device.address.address == 0 || device.type == ScanI2C::NONE) {
            LOG_DEBUG("AccelerometerThread disabling due to no sensors found\n");
            disable();
            return;
        }

#ifndef RAK_4631
        if (!config.display.wake_on_tap_or_motion && !config.device.double_tap_as_button_press) {
            LOG_DEBUG("AccelerometerThread disabling due to no interested configurations\n");
            disable();
            return;
        }
#endif

        switch (device.type) {
        case ScanI2C::DeviceType::BMA423:
            sensor = new BMA423Sensor(device);
            break;
        case ScanI2C::DeviceType::MPU6050:
            sensor = new MPU6050Sensor(device);
            break;
        case ScanI2C::DeviceType::BMX160:
            sensor = new BMX160Sensor(device);
            break;
        case ScanI2C::DeviceType::LIS3DH:
            sensor = new LIS3DHSensor(device);
            break;
        case ScanI2C::DeviceType::LSM6DS3:
            sensor = new LSM6DS3Sensor(device);
            break;
        case ScanI2C::DeviceType::STK8BAXX:
            sensor = new STK8XXXSensor(device);
            break;
        case ScanI2C::DeviceType::ICM20948:
            sensor = new ICM20948Sensor(device);
            break;
        default:
            disable();
            return;
        }

        isInitialised = sensor->init();
        if (!isInitialised) {
            clean();
        }
        LOG_DEBUG("AccelerometerThread::init %s\n", isInitialised ? "ok" : "failed");
    }

    // Copy constructor (not implemented / included to avoid cppcheck warnings)
    AccelerometerThread(const AccelerometerThread &other) : OSThread::OSThread("AccelerometerThread") { this->copy(other); }

    // Destructor (included to avoid cppcheck warnings)
    virtual ~AccelerometerThread() { clean(); }

    // Copy assignment (not implemented / included to avoid cppcheck warnings)
    AccelerometerThread &operator=(const AccelerometerThread &other)
    {
        this->copy(other);
        return *this;
    }

    // Take a very shallow copy (does not copy OSThread state nor the sensor object)
    // If for some reason this is ever used, make sure to call init() after any copy
    void copy(const AccelerometerThread &other)
    {
        if (this != &other) {
            clean();
            this->device = ScanI2C::FoundDevice(other.device.type,
                                                ScanI2C::DeviceAddress(other.device.address.port, other.device.address.address));
        }
    }

    // Cleanup resources
    void clean()
    {
        isInitialised = false;
        if (sensor != nullptr) {
            delete sensor;
            sensor = nullptr;
        }
    }
};

#endif

#endif