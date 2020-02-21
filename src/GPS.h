#pragma once

#include <TinyGPS++.h>
#include "PeriodicTask.h"
#include "Observer.h"
#include "sys/time.h"

/**
 * A gps class that only reads from the GPS periodically (and FIXME - eventually keeps the gps powered down except when reading)
 * 
 * When new data is available it will notify observers.
 */
class GPS : public PeriodicTask, public Observable, public TinyGPSPlus
{
public:
    GPS();

    /// Return time since 1970 in secs.  Until we have a GPS lock we will be returning time based at zero
    uint32_t getTime();

    /// Return time since 1970 in secs.  If we don't have a GPS lock return zero
    uint32_t getValidTime();

    String getTimeStr();

    void setup();

    virtual void loop();

    virtual uint32_t doTask();

    /// If we haven't yet set our RTC this boot, set it from a GPS derived time
    void perhapsSetRTC(const struct timeval *tv);

private:
    void readFromRTC();
};

extern GPS gps;
