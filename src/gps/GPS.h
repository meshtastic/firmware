#pragma once

#include "Observer.h"
#include "PeriodicTask.h"
#include "sys/time.h"

/// If we haven't yet set our RTC this boot, set it from a GPS derived time
void perhapsSetRTC(const struct timeval *tv);

/// Return time since 1970 in secs.  Until we have a GPS lock we will be returning time based at zero
uint32_t getTime();

/// Return time since 1970 in secs.  If we don't have a GPS lock return zero
uint32_t getValidTime();

void readFromRTC();

/**
 * A gps class that only reads from the GPS periodically (and FIXME - eventually keeps the gps powered down except when reading)
 *
 * When new data is available it will notify observers.
 */
class GPS : public Observable<void *>
{
  protected:
    bool hasValidLocation = false; // default to false, until we complete our first read

    static HardwareSerial &_serial_gps;

  public:
    uint32_t latitude = 0, longitude = 0; // as an int mult by 1e-7 to get value as double
    uint32_t altitude = 0;
    bool isConnected = false; // Do we have a GPS we are talking to

    virtual ~GPS() {}

    /**
     * Returns true if we succeeded
     */
    virtual bool setup() = 0;

    /// Returns ture if we have acquired GPS lock.
    bool hasLock() const { return hasValidLocation; }

    /**
     * Restart our lock attempt - try to get and broadcast a GPS reading ASAP
     * called after the CPU wakes from light-sleep state */
    virtual void startLock() {}
};

extern GPS *gps;
