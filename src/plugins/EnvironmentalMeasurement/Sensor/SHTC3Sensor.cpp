#include "../mesh/generated/environmental_measurement.pb.h"
#include "configuration.h"
#include "EnvironmentalMeasurementSensor.h"
#include "SHTC3Sensor.h"
#include <SparkFun_SHTC3.h>

SHTC3Sensor::SHTC3Sensor() : EnvironmentalMeasurementSensor {} {
}

int32_t SHTC3Sensor::runOnce() {

    // default I2C address is 0x70, per Ada Fruit
    g_shtc3.begin(0x70);

    if (g_shtc3.passIDcrc) {
        DEBUG_MSG("SHTC3 ID Passed Checkshum.");
        DEBUG_MSG("Device ID: 0b%s", g_shtc3.ID);
    }
    else {
        DEBUG_MSG("SHTC3 ID Checksum failed.");
    }
    return (MCP_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS);
}

bool SHTC3Sensor::getMeasurement(EnvironmentalMeasurement *measurement) {
    bool gotData = false;

    g_shtc3.update();

    switch (g_shtc3.lastStatus)
    {
        case SHTC3_Status_Nominal:
            measurement->temperature = g_shtc3.toDegC();
            measurement->relative_humidity = g_shtc3.toPercent();
            DEBUG_MSG("SHTC3 Success %.2f %.f", measurement->temperature, measurement->relative_humidity);
            gotData = true;
            break;
        case SHTC3_Status_Error:
            DEBUG_MSG("SHTC3 Error");
            break;
        case SHTC3_Status_CRC_Fail:
            DEBUG_MSG("SHTC3 CRC Fail");
            break;
        default:
            DEBUG_MSG("SHTC3 Unknown return code");
            break;
    }
    return gotData;
}
