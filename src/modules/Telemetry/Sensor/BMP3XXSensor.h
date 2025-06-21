#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && __has_include(<Adafruit_BMP3XX.h>)

#ifndef _BMP3XX_SENSOR_H
#define _BMP3XX_SENSOR_H

#define SEAL_LEVEL_HPA 1013.2f

#include "TelemetrySensor.h"
#include <Adafruit_BMP3XX.h>
#include <typeinfo>

// Singleton wrapper for the Adafruit_BMP3XX class
class BMP3XXSingleton : public Adafruit_BMP3XX
{
  private:
    static BMP3XXSingleton *pinstance;

  protected:
    BMP3XXSingleton();
    ~BMP3XXSingleton();

  public:
    // Create a singleton instance (not thread safe)
    static BMP3XXSingleton *GetInstance();

    // Singletons should not be cloneable.
    BMP3XXSingleton(BMP3XXSingleton &other) = delete;

    // Singletons should not be assignable.
    void operator=(const BMP3XXSingleton &) = delete;

    // Performs a full reading of all sensors in the BMP3XX. Assigns
    // the internal temperature, pressure and altitudeAmsl variables
    bool performReading();

    // Altitude in metres above mean sea level, assigned after calling performReading()
    double altitudeAmslMetres = 0.0f;
};

class BMP3XXSensor : public TelemetrySensor
{
  protected:
    BMP3XXSingleton *bmp3xx = nullptr;
    virtual void setup() override;

  public:
    BMP3XXSensor();
    virtual int32_t runOnce() override;
    virtual bool getMetrics(meshtastic_Telemetry *measurement) override;
};

#endif

#endif