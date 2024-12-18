#pragma once

#ifndef _BMX160_SENSOR_H_
#define _BMX160_SENSOR_H_

#include "MotionSensor.h"

#if !defined(ARCH_PORTDUINO) && !defined(ARCH_STM32WL) && !MESHTASTIC_EXCLUDE_I2C

#ifdef RAK_4631

#include "Fusion/Fusion.h"
#include <Rak_BMX160.h>

#define BMX160_MAX_STATE_BLOB_SIZE (256) // pad size to allow for additional saved config parameters (accel, gyro, etc)

struct xyzFloat {
    float x;
    float y;
    float z;
};
struct minMaxXYZ {
    xyzFloat min;
    xyzFloat max;
};
struct BMX160Config {
    minMaxXYZ mAccel;
};

class BMX160Sensor : public MotionSensor
{
  private:
    RAK_BMX160 sensor;
    bool showingScreen = false;
    BMX160Config bmx160Config;

  protected:
    const char *bmx160ConfigFileName = "/prefs/bmx160.dat";
    uint8_t bmx160State[BMX160_MAX_STATE_BLOB_SIZE] = {0};
    void loadState();
    void saveState();

  public:
    explicit BMX160Sensor(ScanI2C::FoundDevice foundDevice);
    virtual bool init() override;
    virtual int32_t runOnce() override;
    virtual void calibrate(uint16_t forSeconds) override;
};

#else

// Stub
class BMX160Sensor : public MotionSensor
{
  public:
    explicit BMX160Sensor(ScanI2C::FoundDevice foundDevice);
};

#endif

#endif

#endif