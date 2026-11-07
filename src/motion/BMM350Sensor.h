#pragma once

#include "MotionSensor.h"

#if !defined(ARCH_STM32WL) && !MESHTASTIC_EXCLUDE_I2C && !MESHTASTIC_EXCLUDE_MAGNETOMETER && __has_include(<DFRobot_BMM350.h>)

#include <DFRobot_BMM350.h>
#include <memory>

class BMM350Sensor : public MotionSensor
{
  private:
    class Sensor : public DFRobot_BMM350
    {
      private:
        static TwoWire *bus;
        static bool transferFailed;
        static int8_t readRegisters(uint8_t reg, uint8_t *data, uint32_t length, void *context);
        static int8_t writeRegisters(uint8_t reg, const uint8_t *data, uint32_t length, void *context);
        static void delayUs(uint32_t period, void *context);

      public:
        Sensor(TwoWire *wire, uint8_t address);
        uint8_t begin();
        bool configure();
        bool readSample(sBmm350MagData_t &data);
    };

    std::unique_ptr<Sensor> sensor;
    bool showingScreen = false;
    bool seedPending = false;
    static constexpr const char *compassCalibrationFileName = "/prefs/compass_bmm350.dat";
    float highestX = 0, lowestX = 0, highestY = 0, lowestY = 0, highestZ = 0, lowestZ = 0;

    bool readMagnetometer(float &xMicroTesla, float &yMicroTesla, float &zMicroTesla);

  public:
    explicit BMM350Sensor(ScanI2C::FoundDevice foundDevice);
    bool init() override;
    int32_t runOnce() override;
    void calibrate(uint16_t forSeconds) override;
    bool providesHeading() const override { return true; }
};

#endif