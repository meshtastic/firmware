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

  public:
    virtual bool setupGPS();

  protected:
    /** Subclasses should look for serial rx characters here and feed it to their GPS parser
     * 
     * Return true if we received a valid message from the GPS
    */
    virtual bool whileIdle();

    /**
     * Perform any processing that should be done only while the GPS is awake and looking for a fix.
     * Override this method to check for new locations
     *
     * @return true if we've acquired a time
     */
    virtual bool lookForTime();

    /**
     * Perform any processing that should be done only while the GPS is awake and looking for a fix.
     * Override this method to check for new locations
     *
     * @return true if we've acquired a new location
     */
    virtual bool lookForLocation();
};
