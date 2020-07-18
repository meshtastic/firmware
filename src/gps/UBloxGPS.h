#pragma once

#include "GPS.h"
#include "Observer.h"
#include "../concurrency/PeriodicTask.h"
#include "SparkFun_Ublox_Arduino_Library.h"

/**
 * A gps class that only reads from the GPS periodically (and FIXME - eventually keeps the gps powered down except when reading)
 *
 * When new data is available it will notify observers.
 */
class UBloxGPS : public GPS, public concurrency::PeriodicTask
{
    SFE_UBLOX_GPS ublox;

    bool wantNewLocation = true;

    CallbackObserver<UBloxGPS, void *> notifySleepObserver = CallbackObserver<UBloxGPS, void *>(this, &UBloxGPS::prepareSleep);

  public:
    UBloxGPS();

    /**
     * Returns true if we succeeded
     */
    virtual bool setup();

    virtual void doTask();

    /**
     * Restart our lock attempt - try to get and broadcast a GPS reading ASAP
     * called after the CPU wakes from light-sleep state */
    virtual void startLock();

  private:

    /// Prepare the GPS for the cpu entering deep or light sleep, expect to be gone for at least 100s of msecs
    /// always returns 0 to indicate okay to sleep
    int prepareSleep(void *unused);

    /// Attempt to connect to our GPS, returns false if no gps is present
    bool tryConnect();
};
