#include "../mesh/generated/environmental_measurement.pb.h"
#include "configuration.h"
#include "EnvironmentalMeasurementSensor.h"
#include "MCP9808Sensor.h"
#include <Adafruit_MCP9808.h>

MCP9808Sensor::MCP9808Sensor() : EnvironmentalMeasurementSensor {} {
}

int32_t MCP9808Sensor::runOnce() {
    unsigned mcp9808Status;
    // Default i2c address for MCP9808
    mcp9808Status = mcp9808.begin(0x18); 
    if (!mcp9808Status) {
        DEBUG_MSG("Could not find a valid MCP9808 sensor, check wiring, address, sensor ID!");
    } else {
        DEBUG_MSG("EnvironmentalMeasurement: Opened MCP9808 on default i2c bus");
        // Reduce resolution from 0.0625 degrees (precision) to 0.125 degrees (high). 
        mcp9808.setResolution(2);
    }
    return (MCP_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS);
}

bool MCP9808Sensor::getMeasurement(EnvironmentalMeasurement *measurement) {
    measurement->temperature = mcp9808.readTempC();

    return true;
}    