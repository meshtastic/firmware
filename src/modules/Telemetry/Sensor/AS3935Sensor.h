#pragma once

#ifndef _MT_AS3935SENSOR_H
#define _MT_AS3935SENSOR_H
#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && __has_include(<SparkFun_AS3935.h>)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "TelemetrySensor.h"
#include <SparkFun_AS3935.h>

class AS3935Sensor : public TelemetrySensor
{
  private:
    SparkFun_AS3935 *lightning = nullptr;
    uint32_t strikeCountWindow = 0;
    float lastDistanceKm = -1; // sentinel: no valid distance captured this window
    uint32_t windowStartMs = 0;

    void classifyPendingIrq();

  public:
    AS3935Sensor();
    ~AS3935Sensor();
    virtual bool initDevice(TwoWire *bus, ScanI2C::FoundDevice *dev) override;
    virtual bool getMetrics(meshtastic_Telemetry *measurement) override;
    virtual int32_t runOnce() override;
};

#endif
#endif
