#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && defined(HAS_RAKHUB)

#ifndef _RAKSENSORHUB_H
#define _RAKSENSORHUB_H 1

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "CurrentSensor.h"
#include "TelemetrySensor.h"
#include "VoltageSensor.h"

/** 1-Wire SensorHub driver (env:rak_wismesh_sensorhub). IPSO parse/cache in RAKSensorHubUplink.cpp. */
class RAKSensorHub : public TelemetrySensor, VoltageSensor, CurrentSensor
{
  protected:
    virtual void setup() override;
    uint32_t lastRead = 0;

  public:
    RAKSensorHub();
    bool hasSensor() { return true; } // Not an I2C sensor; available when HAS_RAKHUB is defined
    virtual int32_t runOnce() override;
    virtual bool getMetrics(meshtastic_Telemetry *measurement) override;
    virtual uint16_t getBusVoltageMv() override;
    virtual int16_t getCurrentMa() override;
    int getBusBatteryPercent();
    bool isCharging();
    void setLastRead(uint32_t lastRead);
};
extern RAKSensorHub rakSensorHub;

#endif // _RAKSENSORHUB_H
#endif // HAS_RAKHUB