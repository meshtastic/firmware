#include "../mesh/generated/telemetry.pb.h"
#include "TelemetrySensor.h"
#include <Adafruit_MCP9808.h>

#define MCP_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS 1000

class MCP9808Sensor : virtual public TelemetrySensor {
private:
    Adafruit_MCP9808 mcp9808;

public:
    MCP9808Sensor();
    virtual int32_t runOnce() override;
    virtual bool getMeasurement(Telemetry *measurement) override;
};    