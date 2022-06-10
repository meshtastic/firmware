#include "../mesh/generated/telemetry.pb.h"
#include "configuration.h"
#include "TelemetrySensor.h"
#include "MCP9808Sensor.h"
#include <Adafruit_MCP9808.h>

MCP9808Sensor::MCP9808Sensor() : TelemetrySensor {} 
{
}

int32_t MCP9808Sensor::runOnce() {
    unsigned mcp9808Status;
    DEBUG_MSG("Init sensor: TelemetrySensorType_MCP9808\n");
    if (!hasSensor(TelemetrySensorType_MCP9808)) {
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    }

    mcp9808Status = mcp9808.begin(nodeTelemetrySensorsMap[TelemetrySensorType_MCP9808]); 
    if (!mcp9808Status) {
        DEBUG_MSG("Could not connect to detected MCP9808 sensor.\n Removing from nodeTelemetrySensorsMap.\n");
        nodeTelemetrySensorsMap[TelemetrySensorType_MCP9808] = 0;
    } else {
        DEBUG_MSG("TelemetrySensor: Opened MCP9808 on default i2c bus\n");
        // Reduce resolution from 0.0625 degrees (precision) to 0.125 degrees (high). 
        mcp9808.setResolution(2);
    }
    return (DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS);
}

bool MCP9808Sensor::getMeasurement(Telemetry *measurement) {
    DEBUG_MSG("MCP9808Sensor::getMeasurement\n");
    measurement->variant.environment_metrics.temperature = mcp9808.readTempC();
    return true;
}    