#pragma once

#include "NMEAGPS.h"

/**
 * A gps class thatreads from a NMEA GPS stream (and FIXME - eventually keeps the gps powered down except when reading)
 *
 * When new data is available it will notify observers.
 */
class Air530GPS : public NMEAGPS
{
  protected:
    /// If possible force the GPS into sleep/low power mode
    virtual void sleep();

    /// wake the GPS into normal operation mode
    virtual void wake();

  private:
    /// Send a NMEA cmd with checksum
    void sendCommand(const char *str);
};
