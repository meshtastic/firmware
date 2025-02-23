#ifdef HAS_RAKPROT
#ifndef _RAK9154SENSOR_H
#define _RAK9154SENSOR_H 1
#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "../modules/Telemetry/Sensor/TelemetrySensor.h"
#include "../modules/Telemetry/Sensor/VoltageSensor.h"

class RAK9154Sensor : public TelemetrySensor, VoltageSensor
{
  private:
  protected:
    virtual void setup() override;

  public:
    RAK9154Sensor();
    virtual int32_t runOnce() override;
    virtual bool getMetrics(meshtastic_Telemetry *measurement) override;
    virtual uint16_t getBusVoltageMv() override;
    int getBusBatteryPercent();
    bool isCharging();
};
#endif // _RAK9154SENSOR_H
#endif // HAS_RAKPROT