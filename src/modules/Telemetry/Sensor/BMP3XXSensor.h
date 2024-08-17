#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR

#ifndef _BMP3XX_SENSOR_H
#define _BMP3XX_SENSOR_H

#define SEAL_LEVEL_HPA 1013.2f

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "TelemetrySensor.h"
#include <Adafruit_BMP3XX.h>

class BMP3XXSensor : public TelemetrySensor
{
protected:
  Adafruit_BMP3XX bmp3xx;
  float pressureHPa = 0.0f;
  float temperatureCelcius = 0.0f;
  float altitudeAmslMetres = 0.0f;

public:
  BMP3XXSensor();
  virtual int32_t runOnce() override;
  virtual bool getMetrics(meshtastic_Telemetry *measurement) override;
  virtual float getAltitudeAMSL();
};

#endif

#endif