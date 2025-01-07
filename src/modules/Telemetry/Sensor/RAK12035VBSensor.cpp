#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && CAN_HOST_RAK12035VBSOIL

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "RAK12035VBSensor.h"

RAK12035VBSensor::RAK12035VBSensor() : TelemetrySensor(meshtastic_TelemetrySensorType_RAK12035VB, "RAK12035VB_SOIL") {}

int32_t RAK12035VBSensor::runOnce()
{
    LOG_INFO("Init sensor: %s", sensorName);
    if (!hasSensor()) {
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    }

    sensor.set_sensor_addr(RAK12035VB1_ADDR);

    sensor.begin(nodeTelemetrySensorsMap[sensorType].first);
    // Get sensor firmware version
	uint8_t data = 0;
	sensor.get_sensor_version(&data);
	LOG_INFO("Sensor Firmware version: %i", data);

    if ( data != 0)
    {
        LOG_DEBUG("RAK12035VBSensor Init Succeed");
        status = true;
    } else {
        LOG_ERROR("RAK12035VBSensor Init Failed");
        status = false;
    }
    sensor.sensor_sleep();
    return initI2CSensor();
}

void RAK12035VBSensor::setup() {
	// Set the calibration values
	// Reading the saved calibration values from the sensor.
    uint16_t zero_val = 0;
    uint16_t hundred_val = 0;
    uint16_t default_zero_val = 550;
    uint16_t default_hundred_val = 420;
    sensor.sensor_on();
    delay(200);
	sensor.get_dry_cal(&zero_val);
	sensor.get_wet_cal(&hundred_val);
    delay(200);
    if(zero_val == 0 || zero_val <= hundred_val){
        LOG_ERROR("Dry calibration value is %d", zero_val);
        LOG_ERROR("Wet calibration value is %d", hundred_val);
        LOG_ERROR("This does not make sense. Youc can recalibrate this sensor using the calibration sketch included here: https://github.com/RAKWireless/RAK12035_SoilMoisture.");
        LOG_ERROR("For now, setting default calibration value for Dry Calibration: %d", default_zero_val);
        sensor.set_dry_cal(default_zero_val);
        sensor.get_dry_cal(&zero_val);
        LOG_ERROR("Dry calibration reset complete. New value is %d", zero_val);
    }    
    if(hundred_val == 0 || hundred_val >= zero_val){
        LOG_ERROR("Dry calibration value is %d", zero_val);
        LOG_ERROR("Wet calibration value is %d", hundred_val);
        LOG_ERROR("This does not make sense. Youc can recalibrate this sensor using the calibration sketch included here: https://github.com/RAKWireless/RAK12035_SoilMoisture.");
        LOG_ERROR("For now, setting default calibration value for Wet Calibration: %d", default_hundred_val);
        sensor.set_wet_cal(default_hundred_val);
        sensor.get_wet_cal(&hundred_val);
        LOG_ERROR("Wet calibration reset complete. New value is %d", hundred_val);
    }  
    sensor.sensor_sleep();
    delay(200);
	LOG_INFO("Dry calibration value is %d", zero_val);
	LOG_INFO("Wet calibration value is %d", hundred_val);
}

bool RAK12035VBSensor::getMetrics(meshtastic_Telemetry *measurement)
{
    measurement->variant.environment_metrics.has_soil_temperature = true;
    measurement->variant.environment_metrics.has_soil_moisture = true;

	uint8_t moisture = 0;
    uint16_t temp = 0;
    bool success = false;

    sensor.sensor_on();
    delay(200);
    success = sensor.get_sensor_moisture(&moisture);
    delay(200);
    success = sensor.get_sensor_temperature(&temp);
    delay(200);
    sensor.sensor_sleep();
    
    if(success == false){
        LOG_ERROR("Failed to read sensor data");
        return false;
    }
    LOG_INFO("Successful read from sensor Temperature: %.2f, Moisture: %ld%", (double)(temp/10), moisture);
    measurement->variant.environment_metrics.soil_temperature = (float)(temp/10);
    measurement->variant.environment_metrics.soil_moisture = moisture;

    LOG_INFO("Check if the original temperature and moisture (relative_humidity) are being used.. if not just use them for the soil monitoring.");

    if( !measurement->variant.environment_metrics.has_temperature ){
        LOG_INFO("Overwrite the temp metrics (not being set right now and this will allow the soil temp value to be used in the client interface).");
        measurement->variant.environment_metrics.has_temperature = true;
        measurement->variant.environment_metrics.temperature = (float)(temp/10);
    }

    if( !measurement->variant.environment_metrics.has_relative_humidity ){
        LOG_INFO("Overwrite the moisture metrics (not being used for air humidity and this will allow the soil humidity to appear in the client interfaces without adjustments).");
        measurement->variant.environment_metrics.has_relative_humidity = true;
        measurement->variant.environment_metrics.relative_humidity = (float)moisture;
    }

    return true;
}
#endif