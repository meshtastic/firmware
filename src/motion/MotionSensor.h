#pragma once
#ifndef _MOTION_SENSOR_H_
#define _MOTION_SENSOR_H_

#define MOTION_SENSOR_CHECK_INTERVAL_MS 100
#define MOTION_SENSOR_CLICK_THRESHOLD 40

#include "../configuration.h"

#if !defined(ARCH_PORTDUINO) && !defined(ARCH_STM32WL) && !MESHTASTIC_EXCLUDE_I2C

#include "../PowerFSM.h"
#include "../detect/ScanI2C.h"
#include "../graphics/Screen.h"
#include "../graphics/ScreenFonts.h"
#include "../power.h"
#include "FSCommon.h"
#include "Wire.h"

#define MAX_STATE_BLOB_SIZE (256) // pad size to allow for additional saved config parameters (accel, gyro, etc)

struct xyzFloat {
    float x;
    float y;
    float z;
};
struct minMaxXYZ {
    xyzFloat min;
    xyzFloat max;
};
struct SensorConfig {
    minMaxXYZ mAccel;
};

// Base class for motion processing
class MotionSensor
{
  public:
    explicit MotionSensor(ScanI2C::FoundDevice foundDevice);
    virtual ~MotionSensor(){};

    // Get the device type
    ScanI2C::DeviceType deviceType();

    // Get the device address
    uint8_t deviceAddress();

    // Get the device port
    ScanI2C::I2CPort devicePort();

    // Initialise the motion sensor
    inline virtual bool init() { return false; };

    // The method that will be called each time our sensor gets a chance to run
    // Returns the desired period for next invocation (or RUN_SAME for no change)
    // Refer to /src/concurrency/OSThread.h for more information
    inline virtual int32_t runOnce() { return MOTION_SENSOR_CHECK_INTERVAL_MS; };

    virtual void calibrate(uint16_t forSeconds){};

  protected:
    // Turn on the screen when a tap or motion is detected
    virtual void wakeScreen();

    // Register a button press when a double-tap is detected
    virtual void buttonPress();

#if defined(RAK_4631) & !MESHTASTIC_EXCLUDE_SCREEN
    // draw an OLED frame (currently only used by the RAK4631 BMX160 sensor)
    static void drawFrameCalibration(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);
#endif

    ScanI2C::FoundDevice device;

    SensorConfig sensorConfig;
    bool showingScreen = false;
    bool doCalibration = false;
    bool firstCalibrationRead = false;
    uint32_t endCalibrationAt = 0;

    void getMagCalibrationData(float x, float y, float z);

    const char *configFileName = "/prefs/motionSensor.dat";
    uint8_t sensorState[MAX_STATE_BLOB_SIZE] = {0};
    void loadState();
    void saveState();
};

namespace MotionSensorI2C
{

static inline int readRegister(uint8_t address, uint8_t reg, uint8_t *data, uint8_t len)
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

static inline int writeRegister(uint8_t address, uint8_t reg, uint8_t *data, uint8_t len)
{
    Wire.beginTransmission(address);
    Wire.write(reg);
    Wire.write(data, len);
    return (0 != Wire.endTransmission());
}

} // namespace MotionSensorI2C

#endif

#endif