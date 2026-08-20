#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && (defined(T_DECK_MAX) || defined(_VARIANT_T_DECK_PRO_V1_1)) && \
    defined(HAS_LTR553ALS) && __has_include(<SensorLTR553.hpp>)

#include "LTR553ALSSensor.h"
#include "LTR553ALS.h"

LTR553ALSSensor::LTR553ALSSensor() : TelemetrySensor(meshtastic_TelemetrySensorType_SENSOR_UNSET, "LTR553ALS") {}

bool LTR553ALSSensor::initDevice(TwoWire *bus, ScanI2C::FoundDevice *dev)
{
    LOG_INFO("Init sensor: %s", sensorName);

    _bus = bus;
    _address = dev->address.address;
    _port = dev->address.port;

#if defined(I2C_SDA) && defined(I2C_SCL)
    status = sensor.begin(*bus, I2C_SDA, I2C_SCL);
#else
    status = sensor.begin(*bus, -1, -1);
#endif
    if (!status)
        return false;

    sensor.setLightSensorGain(SensorLTR553::ALS_GAIN_1X);
    sensor.setLightSensorRate(SensorLTR553::ALS_INTEGRATION_TIME_100MS,
                              SensorLTR553::ALS_MEASUREMENT_TIME_100MS);
    sensor.enableLightSensor();

    initialized = true;
    LOG_INFO("Opened %s sensor on i2c bus", sensorName);
    return status;
}

bool LTR553ALSSensor::getMetrics(meshtastic_Telemetry *measurement)
{
    const int channel0 = sensor.getLightSensor(0);
    const int channel1 = sensor.getLightSensor(1);
    if (channel0 < 0 || channel1 < 0)
        return false;

    measurement->variant.environment_metrics.has_lux = true;
    measurement->variant.environment_metrics.lux =
        ltr553::luxFromChannels(static_cast<uint16_t>(channel0), static_cast<uint16_t>(channel1));
    LOG_DEBUG("LTR553ALS lux: %f (CH0=%d CH1=%d)", measurement->variant.environment_metrics.lux, channel0, channel1);
    return true;
}

#endif
