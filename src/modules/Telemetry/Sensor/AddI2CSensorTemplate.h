#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR || !MESHTASTIC_EXCLUDE_AIR_QUALITY_SENSOR

#include "TelemetrySensor.h"
#include "detect/ScanI2C.h"
#include "detect/ScanI2CTwoWire.h"
#include <Wire.h>
#include <forward_list>

static std::forward_list<TelemetrySensor *> sensors;

template <typename T> void addSensor(ScanI2C *i2cScanner, ScanI2C::DeviceType type)
{
    ScanI2C::FoundDevice dev = i2cScanner->find(type);
    if (dev.type != ScanI2C::DeviceType::NONE || type == ScanI2C::DeviceType::NONE) {
        TelemetrySensor *sensor = new T();
#if WIRE_INTERFACES_COUNT > 1
        TwoWire *bus = ScanI2CTwoWire::fetchI2CBus(dev.address);
        if (dev.address.port != ScanI2C::I2CPort::WIRE1 && sensor->onlyWire1()) {
            // This sensor only works on Wire (Wire1 is not supported)
            delete sensor;
            return;
        }
#else
        TwoWire *bus = &Wire;
#endif
        if (sensor->initDevice(bus, &dev)) {
            sensors.push_front(sensor);
            return;
        }
        // destroy sensor
        delete sensor;
    }
}
#endif