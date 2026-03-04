#pragma once

#include "NodeDB.h"
#include "meshUtils.h"

class BaseTelemetryModule
{
  protected:
    bool isSensorOrRouter() const { return isSensorOrRouterRole(config.device.role); }
};
