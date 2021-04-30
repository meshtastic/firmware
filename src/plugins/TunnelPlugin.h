#pragma once

#include "SinglePortPlugin.h"
#include "concurrency/OSThread.h"
#include "configuration.h"
#include <Arduino.h>
#include <functional>

class TunnelPlugin : private concurrency::OSThread
{
    bool firstTime = 1;

  public:
    TunnelPlugin();

  protected:
    virtual int32_t runOnce();
};

extern TunnelPlugin *tunnelPlugin;
