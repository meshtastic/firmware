#pragma once

#ifndef _MT_AS3935SENSOR_H
#define _MT_AS3935SENSOR_H
#include "MeshModule.h"
#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && __has_include(<SparkFun_AS3935.h>)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "RollingCounter.h"
#include "TelemetrySensor.h"
#include <SparkFun_AS3935.h>

class AS3935Sensor : public TelemetrySensor
{
  private:
    SparkFun_AS3935 *lightning = nullptr;
    RollingCounter<60UL * 60 * 1000, 5UL * 60 * 1000> strikes;
    float lastDistanceKm = -1; // sentinel: no valid distance captured yet

    void classifyPendingIrq();

  protected:
    const char *as3935ConfigFileName = "/prefs/as3935.dat";
    meshtastic_AS3935Config as3935config = meshtastic_AS3935Config_init_zero;
    bool saveCalibrationData();
    bool loadCalibrationData();

  public:
    AS3935Sensor();
    ~AS3935Sensor();
    virtual bool initDevice(TwoWire *bus, ScanI2C::FoundDevice *dev) override;
    virtual bool getMetrics(meshtastic_Telemetry *measurement) override;
    virtual int32_t runOnce() override;
    // Antenna trim in pF. Rejects anything but a multiple of 8 up to 120, which
    // tuneCap() would silently ignore.
    bool setTuningCap(uint32_t pf);
    AdminMessageHandleResult handleAdminMessage(const meshtastic_MeshPacket &mp, meshtastic_AdminMessage *request,
                                                meshtastic_AdminMessage *response) override;
};

#endif
#endif
