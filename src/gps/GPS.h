#pragma once

#include "GPSStatus.h"
#include "Observer.h"
#include "concurrency/OSThread.h"

// Generate a string representation of DOP
const char *getDOPString(uint32_t dop);

/**
 * A gps class that only reads from the GPS periodically (and FIXME - eventually keeps the gps powered down except when reading)
 *
 * When new data is available it will notify observers.
 */
class GPS : private concurrency::OSThread
{
  private:
    uint32_t lastWakeStartMsec = 0, lastSleepStartMsec = 0, lastWhileActiveMsec = 0;

    /**
     * hasValidLocation - indicates that the position variables contain a complete
     *   GPS location, valid and fresh (< gps_update_interval + gps_attempt_time)
     */
    bool hasValidLocation = false; // default to false, until we complete our first read

    bool isAwake = false; // true if we want a location right now

    bool wakeAllowed = true; // false if gps must be forced to sleep regardless of what time it is

    bool shouldPublish = false; // If we've changed GPS state, this will force a publish the next loop()

    bool hasGPS = false; // Do we have a GPS we are talking to

    uint8_t numSatellites = 0;

    CallbackObserver<GPS, void *> notifySleepObserver = CallbackObserver<GPS, void *>(this, &GPS::prepareSleep);
    CallbackObserver<GPS, void *> notifyDeepSleepObserver = CallbackObserver<GPS, void *>(this, &GPS::prepareDeepSleep);

  public:
    /** If !NULL we will use this serial port to construct our GPS */
    static HardwareSerial *_serial_gps;

    Position p = Position_init_default;

    GPS() : concurrency::OSThread("GPS") {}

    virtual ~GPS();

    /** We will notify this observable anytime GPS state has changed meaningfully */
    Observable<const meshtastic::GPSStatus *> newStatus;

    /**
     * Returns true if we succeeded
     */
    virtual bool setup();

    /// Returns true if we have acquired GPS lock.
    virtual bool hasLock();

    /// Returns true if there's valid data flow with the chip.
    virtual bool hasFlow();

    /// Return true if we are connected to a GPS
    bool isConnected() const { return hasGPS; }

    /**
     * Restart our lock attempt - try to get and broadcast a GPS reading ASAP
     * called after the CPU wakes from light-sleep state
     *
     * Or set to false, to disallow any sort of waking
     * */
    void forceWake(bool on);

    // Some GPS modules (ublock) require factory reset
    virtual bool factoryReset() { return true; }

  protected:
    /// Do gps chipset specific init, return true for success
    virtual bool setupGPS();

    /// If possible force the GPS into sleep/low power mode
    virtual void sleep();

    /// wake the GPS into normal operation mode
    virtual void wake();

    /** Subclasses should look for serial rx characters here and feed it to their GPS parser
     *
     * Return true if we received a valid message from the GPS
     */
    virtual bool whileIdle() = 0;

    /** Idle processing while GPS is looking for lock, called once per secondish */
    virtual void whileActive() {}

    /**
     * Perform any processing that should be done only while the GPS is awake and looking for a fix.
     * Override this method to check for new locations
     *
     * @return true if we've acquired a time
     */
    virtual bool lookForTime() = 0;

    /**
     * Perform any processing that should be done only while the GPS is awake and looking for a fix.
     * Override this method to check for new locations
     *
     * @return true if we've acquired a new location
     */
    virtual bool lookForLocation() = 0;

    /// Record that we have a GPS
    void setConnected();

    void setNumSatellites(uint8_t n);

  private:
    /// Prepare the GPS for the cpu entering deep or light sleep, expect to be gone for at least 100s of msecs
    /// always returns 0 to indicate okay to sleep
    int prepareSleep(void *unused);

    /// Prepare the GPS for the cpu entering deep sleep, expect to be gone for at least 100s of msecs
    /// always returns 0 to indicate okay to sleep
    int prepareDeepSleep(void *unused);

    /**
     * Switch the GPS into a mode where we are actively looking for a lock, or alternatively switch GPS into a low power mode
     *
     * calls sleep/wake
     */
    void setAwake(bool on);

    /** Get how long we should stay looking for each aquisition
     */
    uint32_t getWakeTime() const;

    /** Get how long we should sleep between aqusition attempts
     */
    uint32_t getSleepTime() const;

    bool getACK(uint8_t c, uint8_t i);

    /**
     * Tell users we have new GPS readings
     */
    void publishUpdate();

    virtual int32_t runOnce() override;
};

// Creates an instance of the GPS class. 
// Returns the new instance or null if the GPS is not present.
GPS* createGps();

extern GPS *gps;
