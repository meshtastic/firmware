#pragma once

#include "GPS.h"
#include "Observer.h"
#include "../concurrency/PeriodicTask.h"
#include "TinyGPS++.h"

/**
 * A gps class thatreads from a NEMA GPS stream (and FIXME - eventually keeps the gps powered down except when reading)
 *
 * When new data is available it will notify observers.
 */
class NEMAGPS : public GPS
{
    TinyGPSPlus reader;
    
    uint32_t lastUpdateMsec = 0;

  public:
    virtual void loop();
};
