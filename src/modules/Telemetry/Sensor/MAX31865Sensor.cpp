#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && defined(MAX31865_CS) && __has_include(<Adafruit_MAX31865.h>)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "MAX31865Sensor.h"
#include "TelemetrySensor.h"
#include <Adafruit_MAX31865.h>

#if MAX31865_WIRES == 3
#define MAX31865_WIRE_MODE MAX31865_3WIRE
#elif MAX31865_WIRES == 2
#define MAX31865_WIRE_MODE MAX31865_2WIRE
#elif MAX31865_WIRES == 4
#define MAX31865_WIRE_MODE MAX31865_4WIRE
#else
#error "MAX31865_WIRES must be 2, 3 or 4"
#endif

MAX31865Sensor::MAX31865Sensor()
    : TelemetrySensor(meshtastic_TelemetrySensorType_MAX31865, "MAX31865"), max31865(MAX31865_CS, &MAX31865_SPI)
{
}

// SPI has no address to scan, so write a pattern into the fault threshold registers and read it back.
// An empty bus reads back all zeros or all ones.
bool MAX31865Sensor::probe()
{
    max31865.setThresholds(0x1234, 0x5678);
    bool found = max31865.getLowerThreshold() == 0x1234 && max31865.getUpperThreshold() == 0x5678;
    max31865.setThresholds(0, 0xFFFF);
    return found;
}

bool MAX31865Sensor::initDevice(TwoWire *bus, ScanI2C::FoundDevice *dev)
{
    LOG_INFO("Init sensor: %s", sensorName);
    max31865.begin(MAX31865_WIRE_MODE); // always returns true
    status = probe();
    if (status) {
        LOG_INFO("Opened %s sensor on SPI, CS %d, %d-wire", sensorName, MAX31865_CS, MAX31865_WIRES);
    } else {
        LOG_DEBUG("%s not found on SPI, CS %d", sensorName, MAX31865_CS);
    }
    initialized = true;
    return status;
}

bool MAX31865Sensor::getMetrics(meshtastic_Telemetry *measurement)
{
    LOG_DEBUG("MAX31865 getMetrics");
    uint16_t rtd = max31865.readRTD();
    uint8_t fault = max31865.readFault(MAX31865_FAULT_AUTO);
    max31865.enableBias(false); // the fault detection cycle leaves the bias on
    if (fault) {
        LOG_WARN("MAX31865 fault 0x%02x, check the RTD wiring", fault);
        max31865.clearFault();
        return false;
    }

    measurement->variant.environment_metrics.has_one_wire_temperature_ch0 = true;
    measurement->variant.environment_metrics.one_wire_temperature_ch0 =
        max31865.calculateTemperature(rtd, MAX31865_RNOMINAL, MAX31865_RREF);
    return true;
}

#endif
