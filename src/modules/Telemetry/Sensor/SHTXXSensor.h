#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && __has_include(<SHTSensor.h>)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "TelemetrySensor.h"
#include <SHTSensor.h>

class SHTXXSensor : public TelemetrySensor
{
  private:
    SHTSensor sht;
    TwoWire *_bus{};
    uint8_t _address{};
    SHTSensor::SHTAccuracy accuracy{};
    bool setAccuracy(SHTSensor::SHTAccuracy newAccuracy);

  public:
    SHTXXSensor();
    virtual bool getMetrics(meshtastic_Telemetry *measurement) override;
    virtual bool initDevice(TwoWire *bus, ScanI2C::FoundDevice *dev) override;
    void getSensorVariant(SHTSensor::SHTSensorType);
    const char *sensorVariant{};

    AdminMessageHandleResult handleAdminMessage(const meshtastic_MeshPacket &mp, meshtastic_AdminMessage *request,
                                                meshtastic_AdminMessage *response) override;
};

#endif