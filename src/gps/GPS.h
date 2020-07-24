#pragma once

#include "../concurrency/PeriodicTask.h"
#include "GPSStatus.h"
#include "Observer.h"
#include "sys/time.h"

/// If we haven't yet set our RTC this boot, set it from a GPS derived time
void perhapsSetRTC(const struct timeval *tv);
void perhapsSetRTC(struct tm &t);

// Generate a string representation of DOP
const char *getDOPString(uint32_t dop);

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

  public:
    /** If !NULL we will use this serial port to construct our GPS */
    static HardwareSerial *_serial_gps;

    /** If !0 we will attempt to connect to the GPS over I2C */
    static uint8_t i2cAddress;

    int32_t latitude = 0, longitude = 0; // as an int mult by 1e-7 to get value as double
    int32_t altitude = 0;
    uint32_t dop = 0;     // Diminution of position; PDOP where possible (UBlox), HDOP otherwise (TinyGPS) in 10^2 units (needs
                          // scaling before use)
    uint32_t heading = 0; // Heading of motion, in degrees * 10^-5
    uint32_t numSatellites = 0;

    bool isConnected = false; // Do we have a GPS we are talking to

    virtual ~GPS() {}

    Observable<const meshtastic::GPSStatus *> newStatus;

    /**
     * Returns true if we succeeded
     */
    virtual bool setup() { return true; }

    /// A loop callback for subclasses that need it.  FIXME, instead just block on serial reads
    virtual void loop() {}

    /// Returns ture if we have acquired GPS lock.
    bool hasLock() const { return hasValidLocation; }

    /**
     * Restart our lock attempt - try to get and broadcast a GPS reading ASAP
     * called after the CPU wakes from light-sleep state */
    virtual void startLock() {}
};

extern GPS *gps;
