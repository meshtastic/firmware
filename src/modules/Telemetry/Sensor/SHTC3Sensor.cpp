#include "../mesh/generated/telemetry.pb.h"
#include "configuration.h"
#include "TelemetrySensor.h"
#include "SHTC3Sensor.h"
#include <Adafruit_SHTC3.h>

SHTC3Sensor::SHTC3Sensor() : 
    TelemetrySensor(TelemetrySensorType_SHTC3, "SHTC3") 
{
}

int32_t SHTC3Sensor::runOnce() {
    DEBUG_MSG("Init sensor: %s\n", sensorName);
    if (!hasSensor()) {
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    }
    if (i2cScanMap[SHTC3_ADDR].addr == 0) {
        DEBUG_MSG("SHTC3 not found on i2c bus\n");
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    }
    if(i2cScanMap[SHTC3_ADDR].bus == 1) {
#ifdef I2C_SDA1
        status = shtc3.begin(&Wire1); 
#endif
    } else {
        status = shtc3.begin(&Wire); 
    }
    
    return initI2CSensor();
}

void SHTC3Sensor::setup() 
{
    // Set up oversampling and filter initialization
}

bool SHTC3Sensor::getMetrics(Telemetry *measurement) {
    sensors_event_t humidity, temp;
    shtc3.getEvent(&humidity, &temp);

    measurement->variant.environment_metrics.temperature = temp.temperature;
    measurement->variant.environment_metrics.relative_humidity = humidity.relative_humidity;

    return true;
}    