#pragma once

#include "GPS.h"
#include "Observer.h"
#include "SparkFun_Ublox_Arduino_Library.h"

/**
 * A gps class that only reads from the GPS periodically (and FIXME - eventually keeps the gps powered down except when reading)
 *
 * When new data is available it will notify observers.
 */
class UBloxGPS : public GPS
{
    SFE_UBLOX_GPS ublox;
    uint8_t fixType = 0;

  public:
    UBloxGPS();

    /**
     * Reset our GPS back to factory settings
     *
     * @return true for success
     */
    bool factoryReset();

  protected:
    /**
     * Returns true if we succeeded
     */
    virtual bool setupGPS();

    /** Subclasses should look for serial rx characters here and feed it to their GPS parser
     *
     * Return true if we received a valid message from the GPS
     */
    virtual bool whileIdle();

    /** Idle processing while GPS is looking for lock */
    virtual void whileActive();

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
    virtual bool hasLock();

    /// If possible force the GPS into sleep/low power mode
    virtual void sleep();
    virtual void wake();

  private:
    /// Attempt to connect to our GPS, returns false if no gps is present
    bool tryConnect();

    /// Switch to our desired operating mode and save the settings to flash
    /// returns true for success
    bool setUBXMode();

    uint16_t maxWait() const { return i2cAddress ? 300 : 0; /*If using i2c we must poll with wait */ }
};
