#include "MCP9808Sensor.h"
#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "TelemetrySensor.h"
#include "configuration.h"
#include <Adafruit_MCP9808.h>

MCP9808Sensor::MCP9808Sensor() : TelemetrySensor(meshtastic_TelemetrySensorType_MCP9808, "MCP9808") {}

int32_t MCP9808Sensor::runOnce()
{
    LOG_INFO("Init sensor: %s\n", sensorName);
    if (!hasSensor()) {
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    }
    status = mcp9808.begin(nodeTelemetrySensorsMap[sensorType].first, nodeTelemetrySensorsMap[sensorType].second);
    return initI2CSensor();
}

void MCP9808Sensor::setup()
{
    mcp9808.setResolution(2);
}

bool MCP9808Sensor::getMetrics(meshtastic_Telemetry *measurement)
{
    LOG_DEBUG("MCP9808Sensor::getMetrics\n");
    measurement->variant.environment_metrics.temperature = mcp9808.readTempC();
    return true;
}