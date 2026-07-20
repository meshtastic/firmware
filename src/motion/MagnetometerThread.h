#pragma once
#ifndef _MAGNETOMETER_THREAD_H_
#define _MAGNETOMETER_THREAD_H_

#include "configuration.h"

#if !defined(ARCH_STM32WL) && !MESHTASTIC_EXCLUDE_I2C && !MESHTASTIC_EXCLUDE_MAGNETOMETER

#include "../concurrency/OSThread.h"
#include "MMC5983MASensor.h"
#include "MotionSensor.h"

extern ScanI2C::DeviceAddress magnetometer_found;

class MagnetometerThread : public concurrency::OSThread
{
  private:
    MotionSensor *sensor = nullptr;
    ScanI2C::FoundDevice device;
    bool isInitialised = false;

  public:
    explicit MagnetometerThread(ScanI2C::FoundDevice foundDevice) : OSThread("Magnetometer")
    {
        device = foundDevice;
        init();
    }

    explicit MagnetometerThread(ScanI2C::DeviceType type) : MagnetometerThread(ScanI2C::FoundDevice{type, magnetometer_found}) {}

    void start()
    {
        init();
        setIntervalFromNow(0);
    };

    void calibrate(uint16_t forSeconds)
    {
        if (sensor) {
            sensor->calibrate(forSeconds);
            setIntervalFromNow(0);
        }
    }

  protected:
    int32_t runOnce() override
    {
        canSleep = true;

        if (isInitialised)
            return sensor->runOnce();

        return MOTION_SENSOR_CHECK_INTERVAL_MS;
    }

  private:
    void init()
    {
        if (isInitialised) {
            return;
        }

        if (device.address.port == ScanI2C::I2CPort::NO_I2C || device.address.address == 0 || device.type == ScanI2C::NONE) {
            LOG_DEBUG("MagnetometerThread Disable due to no sensors found");
            disable();
            return;
        }

        switch (device.type) {
        case ScanI2C::DeviceType::MMC5983MA:
            sensor = new MMC5983MASensor(device);
            break;
        default:
            disable();
            return;
        }

        isInitialised = sensor->init();
        if (!isInitialised) {
            clean();
        }
        LOG_DEBUG("MagnetometerThread::init %s", isInitialised ? "ok" : "failed");
    }

    MagnetometerThread(const MagnetometerThread &other) : OSThread::OSThread("Magnetometer") { this->copy(other); }

    virtual ~MagnetometerThread() { clean(); }

    MagnetometerThread &operator=(const MagnetometerThread &other)
    {
        this->copy(other);
        return *this;
    }

    void copy(const MagnetometerThread &other)
    {
        if (this != &other) {
            clean();
            this->device = ScanI2C::FoundDevice(other.device.type,
                                                ScanI2C::DeviceAddress(other.device.address.port, other.device.address.address));
        }
    }

    void clean()
    {
        isInitialised = false;
        delete sensor;
        sensor = nullptr;
    }
};

#endif

#endif
