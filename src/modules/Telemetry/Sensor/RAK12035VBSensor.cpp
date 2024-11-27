#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "RAK12035VBSensor.h"

RAK12035VBSensor::RAK12035VBSensor() : TelemetrySensor(meshtastic_TelemetrySensorType_RAK12035VB, "RAK12035VB_SOIL") {}

int32_t RAK12035VBSensor::runOnce()
{
    LOG_INFO("Init sensor: %s\n", sensorName);
    if (!hasSensor()) {
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    }

    sensor.begin();
    // Get sensor firmware version
	uint8_t data = 0;
	sensor.get_sensor_version(&data);
	LOG_INFO("Sensor Firmware version: %i\n", data);

    if ( data != 0)
    {
        LOG_DEBUG("RAK12035VBSensor Init Succeed\n");
        status = true;
    } else {
        LOG_ERROR("RAK12035VBSensor Init Failed\n");
        status = false;
    }
    return initI2CSensor();
}

void RAK12035VBSensor::setup() {
	// Set the calibration values
	// Reading the saved calibration values from the sensor.
    uint16_t zero_val = 73;
    uint16_t hundred_val = 250;
	sensor.get_dry_cal(&zero_val);
	sensor.get_wet_cal(&hundred_val);
	LOG_INFO("Dry calibration value is %d\n", zero_val);
	LOG_INFO("Wet calibration value is %d\n", hundred_val);
}

bool RAK12035VBSensor::getMetrics(meshtastic_Telemetry *measurement)
{
    measurement->variant.environment_metrics.has_soil_temperature = true;
    measurement->variant.environment_metrics.has_soil_moisture = true;

	uint8_t moisture = 0;
    uint16_t temp = 0;
	sensor.get_sensor_moisture(&moisture);
    sensor.get_sensor_temperature(&temp);
    measurement->variant.environment_metrics.soil_temperature = (double)temp/10.0;
    measurement->variant.environment_metrics.soil_moisture = moisture;
    return true;
}

#endif