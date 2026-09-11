#include "platform/DeviceSensorProvider.h"

#include "modules/Telemetry/Sensor/TelemetrySensor.h"
#include "motion/MotionSensor.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && defined(HAS_LTR553ALS) && __has_include(<SensorLTR553.hpp>)
#include "modules/Telemetry/Sensor/LTR553ALSSensor.h"
#endif

#if !defined(ARCH_STM32WL) && !MESHTASTIC_EXCLUDE_I2C && defined(HAS_BHI260AP) && \
    __has_include(<SensorBHI260AP.hpp>)
#include "motion/BHI260APSensor.h"
#endif

std::unique_ptr<MotionSensor> DeviceSensorProvider::createAccelerometer(const ScanI2C::FoundDevice &device)
{
#if !defined(ARCH_STM32WL) && !MESHTASTIC_EXCLUDE_I2C && defined(HAS_BHI260AP) && \
    __has_include(<SensorBHI260AP.hpp>)
    if (device.type == ScanI2C::DeviceType::BHI260AP)
        return std::make_unique<BHI260APSensor>(device);
#else
    (void)device;
#endif
    return nullptr;
}

std::unique_ptr<TelemetrySensor> DeviceSensorProvider::createEnvironmentalSensor(ScanI2C::DeviceType type)
{
#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && defined(HAS_LTR553ALS) && __has_include(<SensorLTR553.hpp>)
    if (type == ScanI2C::DeviceType::LTR553ALS)
        return std::make_unique<LTR553ALSSensor>();
#else
    (void)type;
#endif
    return nullptr;
}
