#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSO

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "RAK12035VBSensor.h"

RAK12035VBSensor::RAK12035VBSensor() : TelemetrySensor(meshtastic_TelemetrySensorType_RAK12035VB, "RAK12035VB_SOIL") {}

int32_t RAK12035VBSensor::runOnce()
{
    LOG_INFO("Init sensor: %s", sensorName);
    if (!hasSensor()) {
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    }

    sensor.set_sensor_addr(RAK12035VB_ADDR);

    sensor.begin();
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
    return initI2CSensor();
}

void RAK12035VBSensor::setup() {
	// Set the calibration values
	// Reading the saved calibration values from the sensor.
    uint16_t zero_val = 0;
    uint16_t hundred_val = 0;
    uint16_t default_zero_val = 400;
    uint16_t default_hundred_val = 560;
	sensor.get_dry_cal(&zero_val);
	sensor.get_wet_cal(&hundred_val);
    delay(100);
    if(zero_val == 0 || zero_val >= hundred_val){
        LOG_COLOR_W("Dry calibration value is %d", zero_val);
        LOG_COLOR_W("Wet calibration value is %d", hundred_val);
        LOG_COLOR_W("This does not make sense. Youc can recalibrate this sensor using the calibration sketch included here: https://github.com/RAKWireless/RAK12035_SoilMoisture.");
        LOG_COLOR_W("For now, setting default calibration value for Dry Calibration: %d", default_zero_val);
        sensor.set_dry_cal(default_zero_val);
        sensor.get_dry_cal(&zero_val);
        LOG_COLOR_W("Dry calibration reset complete. New value is %d", zero_val);
    }    
    if(hundred_val == 0 || hundred_val <= zero_val){
        LOG_COLOR_W("Dry calibration value is %d", zero_val);
        LOG_COLOR_W("Wet calibration value is %d", hundred_val);
        LOG_COLOR_W("This does not make sense. Youc can recalibrate this sensor using the calibration sketch included here: https://github.com/RAKWireless/RAK12035_SoilMoisture.");
        LOG_COLOR_W("For now, setting default calibration value for Wet Calibration: %d", default_hundred_val);
        sensor.set_wet_cal(default_hundred_val);
        sensor.get_wet_cal(&hundred_val);
        LOG_COLOR_W("Wet calibration reset complete. New value is %d", hundred_val);
    }  
    delay(100);
	LOG_INFO("Dry calibration value is %d", zero_val);
	LOG_INFO("Wet calibration value is %d", hundred_val);
}

bool RAK12035VBSensor::getMetrics(meshtastic_Telemetry *measurement)
{
    measurement->variant.environment_metrics.has_soil_temperature = true;
    measurement->variant.environment_metrics.has_soil_moisture = true;

	uint8_t moisture = 0;
    uint16_t temp = 0;
	if(sensor.get_sensor_moisture(&moisture) && sensor.get_sensor_temperature(&temp)){
        delay(100);
        measurement->variant.environment_metrics.soil_temperature = temp/10.0;
        measurement->variant.environment_metrics.soil_moisture = moisture;
        return true;
    }
    return false;
}

#endif