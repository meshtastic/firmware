#pragma once

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
    uint8_t fixQual = 0;  // fix quality from GPGGA

#ifndef TINYGPS_OPTION_NO_CUSTOM_FIELDS
    // (20210908) TinyGps++ can only read the GPGSA "FIX TYPE" field
    // via optional feature "custom fields", currently disabled (bug #525)
    TinyGPSCustom gsafixtype;  // custom extract fix type from GPGSA
    TinyGPSCustom gsapdop;     // custom extract PDOP from GPGSA
    uint8_t fixType = 0;  // fix type from GPGSA
#endif

  public:
    virtual bool setupGPS() override;

    virtual bool factoryReset() override;

  protected:
    /** Subclasses should look for serial rx characters here and feed it to their GPS parser
     * 
     * Return true if we received a valid message from the GPS
    */
    virtual bool whileIdle() override;

    /**
     * Perform any processing that should be done only while the GPS is awake and looking for a fix.
     * Override this method to check for new locations
     *
     * @return true if we've acquired a time
     */
    virtual bool lookForTime() override;

    /**
     * Perform any processing that should be done only while the GPS is awake and looking for a fix.
     * Override this method to check for new locations
     *
     * @return true if we've acquired a new location
     */
    virtual bool lookForLocation() override;

    virtual bool hasLock() override;

    virtual bool hasFlow() override;
};
