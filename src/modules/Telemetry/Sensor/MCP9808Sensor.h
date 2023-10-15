#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "TelemetrySensor.h"
#include <Adafruit_MCP9808.h>

class MCP9808Sensor : public TelemetrySensor
{
  private:
    Adafruit_MCP9808 mcp9808;

  protected:
    virtual void setup() override;

  public:
    MCP9808Sensor();
    virtual int32_t runOnce() override;
    virtual bool getMetrics(meshtastic_Telemetry *measurement) override;
};