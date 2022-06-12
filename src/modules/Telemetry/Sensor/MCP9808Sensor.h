#include "../mesh/generated/telemetry.pb.h"
#include "TelemetrySensor.h"
#include <Adafruit_MCP9808.h>

class MCP9808Sensor : virtual public TelemetrySensor {
private:
    Adafruit_MCP9808 mcp9808;

protected:
    virtual void setup() override;
    
public:
    MCP9808Sensor();
    virtual int32_t runOnce() override;
    virtual bool getMetrics(Telemetry *measurement) override;
};    