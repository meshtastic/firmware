#pragma once

#include "../concurrency/PeriodicTask.h"
#include "GPS.h"
#include "Observer.h"
#include "TinyGPS++.h"

/**
 * A gps class thatreads from a NMEA GPS stream (and FIXME - eventually keeps the gps powered down except when reading)
 *
 * When new data is available it will notify observers.
 */
class NMEAGPS : public GPS
{
    TinyGPSPlus reader;

    uint32_t lastUpdateMsec = 0;

  public:
    virtual void loop();
};
