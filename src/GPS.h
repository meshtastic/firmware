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

    virtual void doTask();

    /// If we haven't yet set our RTC this boot, set it from a GPS derived time
    void perhapsSetRTC(const struct timeval *tv);

    /// Returns true if we think the board can enter deep or light sleep now (we might be trying to get a GPS lock)
    bool canSleep();

    /// Prepare the GPS for the cpu entering deep or light sleep, expect to be gone for at least 100s of msecs
    void prepareSleep();

private:
    void readFromRTC();
};

extern GPS gps;
