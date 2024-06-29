#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR

#pragma once

class VoltageSensor
{
  public:
    virtual uint16_t getBusVoltageMv() = 0;
    virtual int16_t getCurrentMa() = 0;
};

#endif
