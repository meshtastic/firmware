#pragma once

#include "NodeDB.h"
#include "configuration.h"

class BaseTelemetryModule
{
  protected:
    bool isSensorOrRouterRole() const
    {
        return config.device.role == meshtastic_Config_DeviceConfig_Role_SENSOR ||
               config.device.role == meshtastic_Config_DeviceConfig_Role_ROUTER;
    }
};
