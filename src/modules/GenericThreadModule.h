#pragma once

#include "MeshModule.h"
#include "concurrency/OSThread.h"
#include "configuration.h"
#include <Arduino.h>
#include <functional>

class GenericThreadModule : private concurrency::OSThread
{
    bool firstTime = 1;

  public:
    GenericThreadModule();

  protected:
    unsigned int my_interval = 10000; // interval in millisconds
    virtual int32_t runOnce() override;
};

extern GenericThreadModule *genericThreadModule;
