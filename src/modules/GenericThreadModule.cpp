#include "GenericThreadModule.h"
#include "MeshService.h"
#include "configuration.h"
#include <Arduino.h>

/*
Generic Thread Module allows for the execution of custom code at a set interval.
*/
GenericThreadModule *genericThreadModule;

GenericThreadModule::GenericThreadModule() : concurrency::OSThread("GenericThreadModule") {}

int32_t GenericThreadModule::runOnce()
{

    bool enabled = true;
    if (!enabled)
        return disable();

    if (firstTime) {
        // do something the first time we run
        firstTime = 0;
        LOG_INFO("first time GenericThread running");
    }

    LOG_INFO("GenericThread executing");
    return (my_interval);
}
