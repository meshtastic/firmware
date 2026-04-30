#include "NoiseMeasureModule.h"
#include "mesh/RadioLibInterface.h"

NoiseMeasureModule::NoiseMeasureModule() : concurrency::OSThread("NoiseMeasure") {}

int32_t NoiseMeasureModule::runOnce()
{
    if (RadioLibInterface::instance && RadioLibInterface::instance->runcheck()) {
        return 60 * 1000;
    } else {
        return 2 * 1000;
    }
}
