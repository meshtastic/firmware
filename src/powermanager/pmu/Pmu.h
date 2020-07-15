#pragma once

#include <stdint.h>

namespace powermanager {

class Pmu {
public:
    virtual void init(bool);

    virtual bool isBatteryConnect();

    virtual float getBattVoltage();

    virtual uint8_t getBattPercentage();

    virtual bool isChargeing();

    virtual bool isVBUSPlug();

    virtual bool getHasBattery();
    
    virtual bool getHasUSB();

    virtual bool status();

    virtual void IRQloop();

    virtual void setIRQ(bool);

};

} // namespace powermanager