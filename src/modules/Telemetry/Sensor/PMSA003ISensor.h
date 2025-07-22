#pragma once

#include "TelemetrySensor.h"

#ifndef PMSA003I_I2C_CLOCK_SPEED
#define PMSA003I_I2C_CLOCK_SPEED 100000
#endif

#ifndef PMSA003I_ENABLE_PIN
#define PMSA003I_FRAME_LENGTH  32
#endif

class PMSA003ISensor : public TelemetrySensor
{
public:
    PMSA003ISensor();
    virtual void setup() override;
    virtual int32_t runOnce() override;
    virtual bool restoreClock(uint32_t currentClock);
    virtual bool getMetrics(meshtastic_Telemetry *measurement) override;
    virtual bool isActive();

#ifdef PMSA003I_ENABLE_PIN
    void sleep();
    uint32_t wakeUp();
#endif

private:
    enum class State { IDLE, ACTIVE };
    State state = State::ACTIVE;
    TwoWire * bus;
    uint8_t address;

    uint16_t computedChecksum = 0;
    uint16_t receivedChecksum = 0;

    uint8_t buffer[PMSA003I_FRAME_LENGTH];
};
