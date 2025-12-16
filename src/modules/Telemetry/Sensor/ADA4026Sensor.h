#pragma once
#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && __has_include(<Adafruit_seesaw.h>)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "TelemetrySensor.h"
#include <Adafruit_seesaw.h>

class ADA4026Sensor : public TelemetrySensor
{
private:
    Adafruit_seesaw ss;  // Adafruit seesaw object for the sensor

protected:
    virtual void setup() override;

public:
    ADA4026Sensor();
    virtual int32_t runOnce() override;
    virtual bool getMetrics(meshtastic_Telemetry *measurement) override;
};

#endif
