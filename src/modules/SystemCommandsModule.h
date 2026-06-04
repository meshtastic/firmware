#pragma once

#include "MeshModule.h"
#include "configuration.h"
#include "input/InputBroker.h"
#include <Arduino.h>
#include <functional>

class SystemCommandsModule
{
    CallbackObserver<SystemCommandsModule, const InputEvent *> inputObserver =
        CallbackObserver<SystemCommandsModule, const InputEvent *>(this, &SystemCommandsModule::handleInputEvent);

  public:
    SystemCommandsModule();
    int handleInputEvent(const InputEvent *event);
};

extern SystemCommandsModule *systemCommandsModule;