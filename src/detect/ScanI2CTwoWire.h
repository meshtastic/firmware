#pragma once

#include <map>
#include <memory>
#include <stddef.h>
#include <stdint.h>

#include <Wire.h>

#include "ScanI2C.h"

#include "../concurrency/Lock.h"

class ScanI2CTwoWire : public ScanI2C
{
  public:
    void scanPort(ScanI2C::I2CPort) override;

    ScanI2C::FoundDevice find(ScanI2C::DeviceType) const override;

    bool exists(ScanI2C::DeviceType) const override;

    size_t countDevices() const override;

  protected:
    FoundDevice firstOfOrNONE(size_t, DeviceType[]) const override;

  private:
    typedef struct RegisterLocation {
        DeviceAddress i2cAddress;
        RegisterAddress registerAddress;

        RegisterLocation(DeviceAddress deviceAddress, RegisterAddress registerAddress)
            : i2cAddress(deviceAddress), registerAddress(registerAddress)
        {
        }

    } RegisterLocation;

    typedef uint8_t ResponseWidth;

    std::map<ScanI2C::DeviceAddress, ScanI2C::DeviceType> foundDevices;

    // note: prone to overwriting if multiple devices of a type are added at different addresses (rare?)
    std::map<ScanI2C::DeviceType, ScanI2C::DeviceAddress> deviceAddresses;

    concurrency::Lock lock;

    void printATECCInfo() const;

    uint16_t getRegisterValue(const RegisterLocation &, ResponseWidth) const;

    DeviceType probeOLED(ScanI2C::DeviceAddress) const;

    TwoWire *fetchI2CBus(ScanI2C::DeviceAddress) const;
};
