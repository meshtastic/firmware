#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR

#pragma once
#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "MeshModule.h"
#include "NodeDB.h"
#include "detect/ScanI2C.h"
#include <utility>

#if !ARCH_PORTDUINO
class TwoWire;
#endif

#define DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS 1000
extern std::pair<uint8_t, TwoWire *> nodeTelemetrySensorsMap[_meshtastic_TelemetrySensorType_MAX + 1];

class TelemetrySensor
{
  protected:
    TelemetrySensor(meshtastic_TelemetrySensorType sensorType, const char *sensorName)
    {
        this->sensorName = sensorName;
        this->sensorType = sensorType;
        this->status = 0;
    }

    const char *sensorName;
    meshtastic_TelemetrySensorType sensorType = meshtastic_TelemetrySensorType_SENSOR_UNSET;
    unsigned status;
    bool initialized = false;

    int32_t initI2CSensor()
    {
        if (!status) {
            LOG_WARN("Can't connect to detected %s sensor. Remove from nodeTelemetrySensorsMap", sensorName);
            nodeTelemetrySensorsMap[sensorType].first = 0;
        } else {
            LOG_INFO("Opened %s sensor on i2c bus", sensorName);
            setup();
        }
        initialized = true;
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    }

    // TODO: check is setup used at all?
    virtual void setup() {}

  public:
    virtual ~TelemetrySensor() {}

    virtual AdminMessageHandleResult handleAdminMessage(const meshtastic_MeshPacket &mp, meshtastic_AdminMessage *request,
                                                        meshtastic_AdminMessage *response)
    {
        return AdminMessageHandleResult::NOT_HANDLED;
    }

    // TODO: delete after migration
    bool hasSensor() { return nodeTelemetrySensorsMap[sensorType].first > 0; }

#if WIRE_INTERFACES_COUNT > 1
    // Set to true if Implementation only works first I2C port (Wire)
    virtual bool onlyWire1() { return false; }
#endif
    virtual int32_t runOnce() { return INT32_MAX; }
    virtual bool isInitialized() { return initialized; }
    virtual bool isRunning() { return status > 0; }

    virtual bool getMetrics(meshtastic_Telemetry *measurement) = 0;
    virtual bool initDevice(TwoWire *bus, ScanI2C::FoundDevice *dev) { return false; };
};

#endif