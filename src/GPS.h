#pragma once

#include <TinyGPS++.h>
#include "PeriodicTask.h"
#include "Observer.h"

/**
 * A gps class that only reads from the GPS periodically (and FIXME - eventually keeps the gps powered down except when reading)
 * 
 * When new data is available it will notify observers.
 */
class GPS : public PeriodicTask, public Observable, public TinyGPSPlus
{
public:
    GPS();

    String getTime();

    void setup();

    virtual void loop();

    virtual void doTask();
};

extern GPS gps;
