#pragma once
#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "TelemetrySensor.h"
#include <ClosedCube_OPT3001.h>

class OPT3001Sensor : public TelemetrySensor
{
  private:
    ClosedCube_OPT3001 opt3001;

  protected:
    virtual void setup() override;

  public:
    OPT3001Sensor();
    virtual int32_t runOnce() override;
    virtual bool getMetrics(meshtastic_Telemetry *measurement) override;
};

#endif