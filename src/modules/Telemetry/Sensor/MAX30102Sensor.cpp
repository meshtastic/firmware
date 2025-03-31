#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && !MESHTASTIC_EXCLUDE_HEALTH_TELEMETRY && !defined(ARCH_PORTDUINO)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "MAX30102Sensor.h"
#include "TelemetrySensor.h"
#include <spo2_algorithm.h>

MAX30102Sensor::MAX30102Sensor() : TelemetrySensor(meshtastic_TelemetrySensorType_MAX30102, "MAX30102") {}

int32_t MAX30102Sensor::runOnce()
{
    LOG_INFO("Init sensor: %s", sensorName);
    if (!hasSensor()) {
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    }

    if (max30102.begin(*nodeTelemetrySensorsMap[sensorType].second, _speed, nodeTelemetrySensorsMap[sensorType].first) ==
        true) // MAX30102 init
    {
        byte brightness = 60;   // 0=Off to 255=50mA
        byte sampleAverage = 4; // 1, 2, 4, 8, 16, 32
        byte leds = 2;          // 1 = Red only, 2 = Red + IR
        byte sampleRate = 100;  // 50, 100, 200, 400, 800, 1000, 1600, 3200
        int pulseWidth = 411;   // 69, 118, 215, 411
        int adcRange = 4096;    // 2048, 4096, 8192, 16384

        max30102.enableDIETEMPRDY(); // Enable the temperature ready interrupt
        max30102.setup(brightness, sampleAverage, leds, sampleRate, pulseWidth, adcRange);
        LOG_DEBUG("MAX30102 Init Succeed");
        status = true;
    } else {
        LOG_ERROR("MAX30102 Init Failed");
        status = false;
    }
    return initI2CSensor();
}

void MAX30102Sensor::setup() {}

bool MAX30102Sensor::getMetrics(meshtastic_Telemetry *measurement)
{
    uint32_t ir_buff[MAX30102_BUFFER_LEN];
    uint32_t red_buff[MAX30102_BUFFER_LEN];
    int32_t spo2;
    int8_t spo2_valid;
    int32_t heart_rate;
    int8_t heart_rate_valid;
    float temp = max30102.readTemperature();

    measurement->variant.environment_metrics.temperature = temp;
    measurement->variant.environment_metrics.has_temperature = true;
    measurement->variant.health_metrics.temperature = temp;
    measurement->variant.health_metrics.has_temperature = true;
    for (byte i = 0; i < MAX30102_BUFFER_LEN; i++) {
        while (max30102.available() == false)
            max30102.check();

        red_buff[i] = max30102.getRed();
        ir_buff[i] = max30102.getIR();
        max30102.nextSample();
    }

    maxim_heart_rate_and_oxygen_saturation(ir_buff, MAX30102_BUFFER_LEN, red_buff, &spo2, &spo2_valid, &heart_rate,
                                           &heart_rate_valid);
    LOG_DEBUG("heart_rate=%d(%d), sp02=%d(%d)", heart_rate, heart_rate_valid, spo2, spo2_valid);
    if (heart_rate_valid) {
        measurement->variant.health_metrics.has_heart_bpm = true;
        measurement->variant.health_metrics.heart_bpm = heart_rate;
    } else {
        measurement->variant.health_metrics.has_heart_bpm = false;
    }
    if (spo2_valid) {
        measurement->variant.health_metrics.has_spO2 = true;
        measurement->variant.health_metrics.spO2 = spo2;
    } else {
        measurement->variant.health_metrics.has_spO2 = true;
    }
    return true;
}

#endif