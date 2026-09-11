#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR || !MESHTASTIC_EXCLUDE_AIR_QUALITY_SENSOR

#include "TelemetrySensor.h"
#include "detect/ScanI2C.h"
#include "detect/ScanI2CTwoWire.h"
#include <Wire.h>
#include <forward_list>
#include <memory>
#include <utility>

static std::forward_list<TelemetrySensor *> sensors;

inline void addSensorInstance(const ScanI2C *i2cScanner, ScanI2C::DeviceType type,
                              std::unique_ptr<TelemetrySensor> sensor)
{
    if (!sensor)
        return;

    ScanI2C::FoundDevice dev = i2cScanner->find(type);
    // Avoid adding the same device twice
    if (dev.type != ScanI2C::DeviceType::NONE) {
        for (const TelemetrySensor *_sensor : sensors) {
            if ((_sensor->_address == dev.address.address) && (_sensor->_port == dev.address.port)) {
                return;
            }
        }
    }

    if (dev.type == ScanI2C::DeviceType::NONE && type != ScanI2C::DeviceType::NONE)
        return;

#if WIRE_INTERFACES_COUNT > 1
    TwoWire *bus = ScanI2CTwoWire::fetchI2CBus(dev.address);
    if (dev.address.port != ScanI2C::I2CPort::WIRE1 && sensor->onlyWire1()) {
        return;
    }
#else
    TwoWire *bus = &Wire;
#endif
    if (sensor->initDevice(bus, &dev))
        sensors.push_front(sensor.release());
}

template <typename T> void addSensor(const ScanI2C *i2cScanner, ScanI2C::DeviceType type)
{
    addSensorInstance(i2cScanner, type, std::make_unique<T>());
}
#endif
