#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && defined(LD8001H_RX)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "TelemetrySensor.h"

// Hi-Link HLK-LD8001H radar level sensor on a UART (115200 8N1, ASCII "R=<raw m> R=<filtered m>").
// Built only with -DLD8001H_RX=<pin>; optional LD8001H_TX and LD8001H_SERIAL (default Serial2).
class LD8001HSensor : public TelemetrySensor
{
  public:
    LD8001HSensor();
    virtual bool getMetrics(meshtastic_Telemetry *measurement) override;
    virtual bool initDevice(TwoWire *bus, ScanI2C::FoundDevice *dev) override;
};

#endif
