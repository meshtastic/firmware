#include "configuration.h"
#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && __has_include("RAK12035_SoilMoisture.h") && defined(RAK_4631) && RAK_4631 == 1

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "RAK12035Sensor.h"

RAK12035Sensor::RAK12035Sensor() : TelemetrySensor(meshtastic_TelemetrySensorType_RAK12035, "RAK12035") {}

int32_t RAK12035Sensor::runOnce()
{
    if (!hasSensor()) {
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    }

    // TODO:: check for up to 2 additional sensors and start them if present.
    sensor.set_sensor_addr(RAK120351_ADDR);
    delay(100);
    sensor.begin(nodeTelemetrySensorsMap[sensorType].first);

    // Get sensor firmware version
    uint8_t data = 0;
    sensor.get_sensor_version(&data);
    if (data != 0) {
        LOG_INFO("Init sensor: %s", sensorName);
        LOG_INFO("RAK12035Sensor Init Succeed \nSensor1 Firmware version: %i, Sensor Name: %s", data, sensorName);
        status = true;
        sensor.sensor_sleep();
    } else {
        // If we reach here, it means the sensor did not initialize correctly.
        LOG_INFO("Init sensor: %s", sensorName);
        LOG_ERROR("RAK12035Sensor Init Failed");
        status = false;
    }

    return initI2CSensor();
}

void RAK12035Sensor::setup()
{
    // Set the calibration values
    // Reading the saved calibration values from the sensor.
    // TODO:: Check for and run calibration check for up to 2 additional sensors if present.
    uint16_t zero_val = 0;
    uint16_t hundred_val = 0;
    uint16_t default_zero_val = 550;
    uint16_t default_hundred_val = 420;
    sensor.sensor_on();
    delay(200);
    sensor.get_dry_cal(&zero_val);
    sensor.get_wet_cal(&hundred_val);
    delay(200);
    if (zero_val == 0 || zero_val <= hundred_val) {
        LOG_INFO("Dry calibration value is %d", zero_val);
        LOG_INFO("Wet calibration value is %d", hundred_val);
        LOG_INFO("This does not make sense. You can recalibrate this sensor using the calibration sketch included here: "
                 "https://github.com/RAKWireless/RAK12035_SoilMoisture.");
        LOG_INFO("For now, setting default calibration value for Dry Calibration: %d", default_zero_val);
        sensor.set_dry_cal(default_zero_val);
        sensor.get_dry_cal(&zero_val);
        LOG_INFO("Dry calibration reset complete. New value is %d", zero_val);
    }
    if (hundred_val == 0 || hundred_val >= zero_val) {
        LOG_INFO("Dry calibration value is %d", zero_val);
        LOG_INFO("Wet calibration value is %d", hundred_val);
        LOG_INFO("This does not make sense. You can recalibrate this sensor using the calibration sketch included here: "
                 "https://github.com/RAKWireless/RAK12035_SoilMoisture.");
        LOG_INFO("For now, setting default calibration value for Wet Calibration: %d", default_hundred_val);
        sensor.set_wet_cal(default_hundred_val);
        sensor.get_wet_cal(&hundred_val);
        LOG_INFO("Wet calibration reset complete. New value is %d", hundred_val);
    }
    sensor.sensor_sleep();
    delay(200);
    LOG_INFO("Dry calibration value is %d", zero_val);
    LOG_INFO("Wet calibration value is %d", hundred_val);
}

bool RAK12035Sensor::getMetrics(meshtastic_Telemetry *measurement)
{
    // TODO:: read and send metrics for up to 2 additional soil monitors if present.
    //  -- how to do this.. this could get a little complex..
    //     ie - 1> we combine them into an average and send that, 2> we send them as separate metrics
    //      ^-- these scenarios would require different handling of the metrics in the receiving end and maybe a setting in the
    //      device ui and an additional proto for that?
    measurement->variant.environment_metrics.has_soil_temperature = true;
    measurement->variant.environment_metrics.has_soil_moisture = true;

    uint8_t moisture = 0;
    uint16_t temp = 0;
    bool success = false;

    sensor.sensor_on();
    delay(200);
    success = sensor.get_sensor_moisture(&moisture);
    delay(200);
    success &= sensor.get_sensor_temperature(&temp);
    delay(200);
    sensor.sensor_sleep();

    if (success == false) {
        LOG_ERROR("Failed to read sensor data");
        return false;
    }
    measurement->variant.environment_metrics.soil_temperature = ((float)temp / 10.0f);
    measurement->variant.environment_metrics.soil_moisture = moisture;

    return true;
}
#endif
