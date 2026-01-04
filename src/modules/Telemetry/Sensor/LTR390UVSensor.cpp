#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && __has_include(<Adafruit_LTR390.h>)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "LTR390UVSensor.h"
#include "TelemetrySensor.h"
#include <Adafruit_LTR390.h>

LTR390UVSensor::LTR390UVSensor() : TelemetrySensor(meshtastic_TelemetrySensorType_LTR390UV, "LTR390UV") {}

bool LTR390UVSensor::initDevice(TwoWire *bus, ScanI2C::FoundDevice *dev)
{
    LOG_INFO("Init sensor: %s", sensorName);

    status = ltr390uv.begin(bus);
    if (!status) {
        return status;
    }

    ltr390uv.setMode(LTR390_MODE_UVS);
    ltr390uv.setGain(LTR390_GAIN_18);                // Datasheet default
    ltr390uv.setResolution(LTR390_RESOLUTION_20BIT); // Datasheet default

    initI2CSensor();
    return status;
}

bool LTR390UVSensor::getMetrics(meshtastic_Telemetry *measurement)
{
    LOG_DEBUG("LTR390UV getMetrics");

    // Because the sensor does not measure Lux and UV at the same time, we need to read them in two passes.
    if (ltr390uv.newDataAvailable()) {
        measurement->variant.environment_metrics.has_lux = true;
        measurement->variant.environment_metrics.has_uv_lux = true;

        if (ltr390uv.getMode() == LTR390_MODE_ALS) {
            lastLuxReading = 0.6 * ltr390uv.readALS() / (1 * 4); // Datasheet page 23 for gain x1 and 20bit resolution
            LOG_DEBUG("LTR390UV Lux reading: %f", lastLuxReading);

            measurement->variant.environment_metrics.lux = lastLuxReading;
            measurement->variant.environment_metrics.uv_lux = lastUVReading;

            ltr390uv.setGain(
                LTR390_GAIN_18); // Recommended for UVI - x18. Do not change, 2300 UV Sensitivity only specified for x18 gain
            ltr390uv.setMode(LTR390_MODE_UVS);

            return true;

        } else if (ltr390uv.getMode() == LTR390_MODE_UVS) {
            lastUVReading = ltr390uv.readUVS() /
                            2300.f; // Datasheet page 23 and page 6, only characterisation for gain x18 and 20bit resolution
            LOG_DEBUG("LTR390UV UV reading: %f", lastUVReading);

            measurement->variant.environment_metrics.lux = lastLuxReading;
            measurement->variant.environment_metrics.uv_lux = lastUVReading;

            ltr390uv.setGain(
                LTR390_GAIN_1); // x1 gain will already max out the sensor at direct sunlight, so no need to increase it
            ltr390uv.setMode(LTR390_MODE_ALS);

            return true;
        }
    }

    // In case we fail to read the sensor mode, set the has_lux and has_uv_lux back to false
    measurement->variant.environment_metrics.has_lux = false;
    measurement->variant.environment_metrics.has_uv_lux = false;

    return false;
}
#endif