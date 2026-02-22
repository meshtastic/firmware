#include "configuration.h"
#include <memory>

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && __has_include(<Adafruit_VL53L0X.h>)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "TelemetrySensor.h"
#include "VL53L0XSensor.h"

#include <Adafruit_VL53L0X.h>
#include <typeinfo>

VL53L0XSensor::VL53L0XSensor() : TelemetrySensor(meshtastic_TelemetrySensorType_VL53L0X, "VL53L0X") {}

bool VL53L0XSensor::initDevice(TwoWire *bus, ScanI2C::FoundDevice *dev)
{
    LOG_INFO("Init sensor: %s", sensorName);
    status = vl53l0x.begin(VL53L0X_I2C_ADDR, false, bus);
    if (!status) {
        return status;
    }

    initI2CSensor();
    return status;
}

bool VL53L0XSensor::getMetrics(meshtastic_Telemetry *measurement)
{
    VL53L0X_RangingMeasurementData_t measure;
    vl53l0x.rangingTest(&measure, false);

    measurement->variant.environment_metrics.has_distance = true;
    measurement->variant.environment_metrics.distance = measure.RangeMilliMeter;

    LOG_INFO("distance %f", measurement->variant.environment_metrics.distance);

    return true;
}
#endif
