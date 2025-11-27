#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && __has_include(<Adafruit_MCP9808.h>)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "MCP9808Sensor.h"
#include "TelemetrySensor.h"
#include <Adafruit_MCP9808.h>

MCP9808Sensor::MCP9808Sensor() : TelemetrySensor(meshtastic_TelemetrySensorType_MCP9808, "MCP9808") {}

bool MCP9808Sensor::initDevice(TwoWire *bus, ScanI2C::FoundDevice *dev)
{
    LOG_INFO("Init sensor: %s", sensorName);
    status = mcp9808.begin(dev->address.address, bus);
    if (!status) {
        return status;
    }
    mcp9808.setResolution(2);

    initI2CSensor();
    return status;
}

bool MCP9808Sensor::getMetrics(meshtastic_Telemetry *measurement)
{
    measurement->variant.environment_metrics.has_temperature = true;

    LOG_DEBUG("MCP9808 getMetrics");
    measurement->variant.environment_metrics.temperature = mcp9808.readTempC();
    return true;
}

#endif